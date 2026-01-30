#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/task/Context.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <fmt/format.h>
#include <asp/collections/SmallVec.hpp>

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

IoWaiter::IoWaiter(Waker waker, uint64_t id, Interest interest)
    : waker(std::move(waker)), id(id), interest(interest) {}
IoWaiter::IoWaiter(std23::move_only_function<void()> eventCallback, uint64_t id, Interest interest)
    : eventCallback(std::move(eventCallback)), id(id), interest(interest) {}

bool IoWaiter::willWake(const Waker& other) const {
    return waker && waker->equals(other);
}

bool IoWaiter::satisfiedBy(Interest ready) const {
    return (ready & interest) != 0;
}

void IoWaiter::wake() {
    if (waker) {
        // wake consumes the waker, so set it back to null
        waker->wake();
        waker.reset();
    }
    if (eventCallback) eventCallback();
}

Registration::Registration(std::shared_ptr<IoEntry> rio) : rio(std::move(rio)) {}

Interest Registration::pollReady(Interest interest, uint64_t& outId) {
    // not in a runtime?
    if (!ctx().m_runtime) {
        return 0;
    }

    auto curr = rio->readiness.load(acquire);

    trace("IoDriver: fd {} readiness: {}", fmtFd(rio->fd), curr);

    uint8_t readiness = (curr & static_cast<uint8_t>(interest));

    if (readiness != 0) {
        return readiness;
    }

    // lock the wait list
    auto waiters = rio->waiters.lock();

    // check for a lost wakeup now that we are holding the lock
    curr = rio->readiness.load(acquire);
    readiness = (curr & static_cast<uint8_t>(interest));
    if (readiness != 0) {
        return readiness;
    }

    // if the id is nonzero, assume we are already registered,
    // so only make sure that our waker is non null
    if (outId != 0) {
        auto it = std::find_if(waiters->begin(), waiters->end(), [outId](const IoWaiter& waiter) {
            return waiter.id == outId;
        });

        ARC_ASSERT(it != waiters->end(), "IoDriver: pollReady called with invalid registration id");
        if (!it->waker) {
            it->waker = ctx().m_waker ? std::make_optional(ctx().m_waker->clone()) : std::nullopt;
        }

        return 0;
    }

    outId = nextId();
    waiters->emplace_back(IoWaiter(ctx().cloneWaker(), outId, interest));
    waiters.unlock();

    trace("IoDriver: added waiter for fd {}: interest {}", fmtFd(rio->fd), static_cast<uint8_t>(interest));

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

    auto it = std::find_if(waiters->begin(), waiters->end(), [id](const IoWaiter& waiter) {
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

IoDriver::IoDriver(std::weak_ptr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    ARC_DEBUG_ASSERT(!m_runtime.expired());
}

IoDriver::~IoDriver() {}

Registration IoDriver::registerIo(SockFd fd, Interest interest) {
    auto entry = std::make_shared<IoEntry>();
    entry->fd = fd;
    entry->runtime = m_runtime;

    trace("IoDriver: registered fd {}", fmtFd(fd));

    m_ioPendingQueue.lock()->push_back(entry);
    return Registration{std::move(entry)};
}

void IoDriver::unregisterIo(const Registration& rio) {
    this->unregisterIo(rio.rio->fd);
}

void IoDriver::unregisterIo(SockFd fd) {
    auto ios = m_ios.lock();

    auto it = ios->find(fd);
    if (it != ios->end()) {
        ios->erase(it);
    }

    trace("IoDriver: unregistered fd {}", fmtFd(fd));
}

void IoDriver::doWork() {
    ARC_POLLFD fds[64];
    IoEntry* entries[64];
    int count = 0;

    auto ios = m_ios.lock();

    // move pending ios to main list
    {
        auto pending = m_ioPendingQueue.lock();
        for (auto& reg : *pending) {
            ARC_DEBUG_ASSERT(reg);
            ios->emplace(reg->fd, std::move(reg));
        }
        pending->clear();
    }

    for (auto& [fd, rio] : *ios) {
        ARC_DEBUG_ASSERT(rio && fd == rio->fd);

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

        entries[count] = rio.get();
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

        arc::printWarn("{}", msg);
        arc::printWarn("Polled fds:");
        for (int i = 0; i < count; i++) {
            auto pfd = fds[i].fd;
            arc::printWarn(" - {}", fmtFd(pfd));
        }
        return;
    }

    trace("IoDriver: poll returned {} fds", ret);

    for (int i = 0; i < count; i++) {
        auto rio = entries[i];
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
            if (waiter.satisfiedBy(ready)) {
                trace("IoDriver: will wake waker id {}", waiter.getId());
                waiter.wake();
            }
        }
    }
}

}
