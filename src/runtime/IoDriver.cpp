#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/task/Context.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <fmt/format.h>

#ifdef _WIN32
# include <winsock2.h>
# define ARC_POLL WSAPoll
# define ARC_POLLFD WSAPOLLFD
#else
# include <sys/poll.h>
# define ARC_POLL ::poll
# define ARC_POLLFD struct pollfd
#endif

using enum std::memory_order;

namespace arc {

#ifdef _WIN32
auto fmtFd(SockFd fd) {
    return (const void*)fd;
}
#else
auto fmtFd(SockFd fd) {
    return (int)fd;
}
#endif

static uint64_t nextId() {
    static std::atomic<uint64_t> m_nextId{1};
    return m_nextId.fetch_add(1, std::memory_order::relaxed);
}

bool RegisteredIoWaiter::satisfiedBy(Interest ready) const {
    return (ready & interest) != 0;
}

Interest Registration::pollReady(Interest interest, uint64_t& outId) {
    ARC_ASSERT(rio);
    ARC_ASSERT(interest != Interest::ReadWrite);

    auto curr = rio->readiness.load(acquire);

    trace("IoDriver: fd {} readiness: {}", fmtFd(rio->fd), curr);

    uint8_t readiness = (curr & static_cast<uint8_t>(interest));

    if (readiness != 0) {
        return readiness;
    }

    // if the id is nonzero, assume we are already registered
    if (outId != 0) {
        return 0;
    }

    outId = nextId();

    auto waiters = rio->waiters.lock();
    waiters->push_back(RegisteredIoWaiter {
        .waker = ctx().m_waker ? std::make_optional(ctx().m_waker->clone()) : std::nullopt,
        .id = outId,
        .interest = interest,
    });
    waiters.unlock();

    trace("IoDriver: added waiter for fd {}: id {}, interest {}", fmtFd(rio->fd), outId, static_cast<uint8_t>(interest));

    if ((interest & Interest::Readable) != 0) {
        rio->anyRead.store(true, release);
    }

    if ((interest & Interest::Writable) != 0) {
        rio->anyWrite.store(true, release);
    }

    return 0;
}

void Registration::unregister(uint64_t id) {
    ARC_ASSERT(rio);
    if (id == 0) return;

    auto waiters = rio->waiters.lock();

    auto it = std::find_if(waiters->begin(), waiters->end(), [id](const RegisteredIoWaiter& waiter) {
        return waiter.id == id;
    });

    if (it == waiters->end()) {
        return;
    }

    trace("IoDriver: removed waiter for fd {}, id {}", fmtFd(rio->fd), id);

    waiters->erase(it);

    // update anyRead/anyWrite
    bool hasRead = false;
    bool hasWrite = false;
    for (auto& waiter : *waiters) {
        if ((waiter.interest & Interest::Readable) != 0) {
            hasRead = true;
        }
        if ((waiter.interest & Interest::Writable) != 0) {
            hasWrite = true;
        }

        if (hasRead && hasWrite) {
            break;
        }
    }

    rio->anyRead.store(hasRead, release);
    rio->anyWrite.store(hasWrite, release);
}

void Registration::clearReadiness(Interest interest) {
    ARC_ASSERT(rio);
    ARC_ASSERT(interest != Interest::ReadWrite);

    trace("IoDriver: clearing readiness for fd {}, interest {}", fmtFd(rio->fd), static_cast<uint8_t>(interest));

    auto curr = rio->readiness.load(acquire);
    uint8_t newReady = curr & ~static_cast<uint8_t>(interest);
    rio->readiness.store(newReady, release);
}

IoDriver::IoDriver(Runtime* runtime) : m_runtime(runtime) {}

IoDriver::~IoDriver() {}

Registration IoDriver::registerIo(SockFd fd, Interest interest) {
    auto rio = std::make_shared<RegisteredIo>();
    rio->fd = fd;

    trace("IoDriver: registered fd {}", fmtFd(fd));

    Registration reg{std::move(rio)};

    m_ioPendingQueue.lock()->push_back(reg);
    return reg;
}

void IoDriver::unregisterIo(const std::shared_ptr<RegisteredIo>& rio) {
    if (ctx().runtime()->isShuttingDown()) return;

    auto ios = m_ios.lock();

    auto it = std::find(ios->begin(), ios->end(), rio);
    if (it != ios->end()) {
        ios->erase(it);
    }

    trace("IoDriver: unregistered fd {}", fmtFd(rio->fd));
}

void IoDriver::doWork() {
    ARC_POLLFD fds[64];
    uint16_t indices[64];
    int count = 0;

    auto ios = m_ios.lock();

    // move pending ios to main list
    {
        auto pending = m_ioPendingQueue.lock();
        for (auto& reg : *pending) {
            ios->push_back(reg.rio);
        }
        pending->clear();
    }

    for (size_t i = 0; i < std::min<size_t>(ios->size(), 64); i++) {
        auto& rio = (*ios)[i];

        bool read = rio->anyRead.load(std::memory_order::acquire);
        bool write = rio->anyWrite.load(std::memory_order::acquire);

        // trace("IoDriver: fd {} - poll read: {}, poll write: {}", fmtFd(rio->fd), read, write);

        if (!read && !write) {
            continue;
        }

        fds[count].fd = rio->fd;
        fds[count].events = 0;
        fds[count].revents = 0;
        if (read) fds[count].events |= POLLIN;
        if (write) fds[count].events |= POLLOUT;

        indices[count] = i;
        count++;
    }

    if (count == 0) {
        // nothing to do
        return;
    }

    int ret = ARC_POLL(fds, count, 0);

    if (ret == 0) {
        return;
    } else if (ret < 0) {
#ifndef _WIN32
        // ignore interrupt
        if (errno == EINTR) {
            return;
        }

        auto msg = fmt::format("Error in IO driver: poll failed: [errno {}] {}", errno, strerror(errno));
#else
        auto msg = fmt::format("Error in IO driver: poll failed: {}", WSAGetLastError());
#endif

        arc::printError("{}", msg);
        arc::printError("Polled fds:");
        for (int i = 0; i < count; i++) {
            auto pfd = fds[i].fd;
            arc::printError(" - {}", fmtFd(pfd));
        }
        return;
    }

    trace("IoDriver: poll returned {} fds", ret);

    static thread_local std::vector<Waker> toWake;

    for (int i = 0; i < count; i++) {
        auto& rio = (*ios)[indices[i]];
        auto& pfd = fds[i];

        Interest ready{};
        if (pfd.revents & POLLIN) {
            ready |= Interest::Readable;
        }
        if (pfd.revents & POLLOUT) {
            ready |= Interest::Writable;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ready |= Interest::Error;
        }

        auto newReadiness = rio->readiness.fetch_or(ready, release) | static_cast<uint8_t>(ready);
        trace("IoDriver: fd {} - readiness {}", fmtFd(rio->fd), newReadiness);

        auto waiters = rio->waiters.lock();
        for (auto& waiter : *waiters) {
            if (waiter.satisfiedBy(ready) && waiter.waker) {
                trace("IoDriver: will wake waker {} (id {})", (void*)waiter.waker->m_data, waiter.id);
                toWake.push_back(std::move(waiter.waker).value());
                waiter.waker.reset();
            }
        }
    }

    ios.unlock();

    for (auto& waker : toWake) {
        waker.wake();
    }
    toWake.clear();
}

}
