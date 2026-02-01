#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>
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
IoWaiter::IoWaiter(arc::MoveOnlyFunction<void()> eventCallback, uint64_t id, Interest interest)
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

Registration::Registration(asp::SharedPtr<IoEntry> rio, IoDriver* driver)
    : m_rio(std::move(rio)), m_driver(driver) {}

Registration::operator bool() const {
    return static_cast<bool>(m_rio);
}

Registration::~Registration() {
    this->reset();
}

Interest Registration::pollReady(Interest interest, Context& cx, uint64_t& outId) {
    ARC_DEBUG_ASSERT(m_rio);
    return m_driver->pollReady(*m_rio, interest, cx, outId);
}

void Registration::unregister(uint64_t id) {
    ARC_DEBUG_ASSERT(m_rio);
    if (id != 0) m_driver->unregisterWaiter(*m_rio, id);
}

void Registration::clearReadiness(Interest interest) {
    ARC_DEBUG_ASSERT(m_rio);
    m_driver->clearReadiness(*m_rio, interest);
}

SockFd Registration::fd() const {
    ARC_DEBUG_ASSERT(m_rio);
    return m_driver->fdForEntry(*m_rio);
}

void Registration::reset() {
    if (m_rio) {
        m_driver->dropRegistration(*this);
    }
    m_rio.reset();
}

// IO driver implementation

IoDriver::IoDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    ARC_DEBUG_ASSERT(!m_runtime.expired());

    static const IoDriverVtable vtable {
        .m_registerIo = &IoDriver::vRegisterIo,
        .m_dropRegistration = &IoDriver::vDropRegistration,
        .m_clearReadiness = &IoDriver::vClearReadiness,
        .m_pollReady = &IoDriver::vPollReady,
        .m_unregisterWaiter = &IoDriver::vUnregisterWaiter,
        .m_fdForEntry = &IoDriver::vFdForEntry,
    };

    m_vtable = &vtable;
}

IoDriver::~IoDriver() {}

Registration IoDriver::registerIo(SockFd fd, Interest interest) {
    return m_vtable->m_registerIo(this, fd, interest);
}

void IoDriver::dropRegistration(const Registration& rio) {
    m_vtable->m_dropRegistration(this, rio);
}

void IoDriver::clearReadiness(IoEntry& rio, Interest interest) {
    m_vtable->m_clearReadiness(this, rio, interest);
}

Interest IoDriver::pollReady(IoEntry& rio, Interest interest, Context& cx, uint64_t& outId) {
    return m_vtable->m_pollReady(this, rio, interest, cx, outId);
}

void IoDriver::unregisterWaiter(IoEntry& rio, uint64_t id) {
    m_vtable->m_unregisterWaiter(this, rio, id);
}

SockFd IoDriver::fdForEntry(const IoEntry& rio) {
    return m_vtable->m_fdForEntry(rio);
}

// IoDriver actual impl

Registration IoDriver::vRegisterIo(IoDriver* self, SockFd fd, Interest interest) {
    auto ios = self->m_ios.lock();
    auto it = ios->find(fd);
    if (it != ios->end()) {
        trace("IoDriver: returning already registered entry for fd {}", fmtFd(fd));
        it->second->registrations.fetch_add(1, std::memory_order::relaxed);
        return Registration{it->second, self};
    }

    auto entry = asp::make_shared<IoEntry>();
    entry->fd = fd;
    entry->runtime = self->m_runtime;
    ios->emplace(fd, entry);
    ios.unlock();

    trace("IoDriver: registered fd {}", fmtFd(fd));

    return Registration{std::move(entry), self};
}

void IoDriver::vDropRegistration(IoDriver* self, const Registration& rio) {
    auto rt = rio.m_rio->runtime.upgrade();
    if (!rt || rt->isShuttingDown()) return;

    auto ios = self->m_ios.lock();
    auto it = ios->find(rio.fd());
    if (it == ios->end()) {
        printWarn("IoDriver: attempted to drop registration for unknown fd {}", fmtFd(rio.fd()));
        return;
    }

    size_t newRegs = it->second->registrations.fetch_sub(1, std::memory_order::relaxed) - 1;
    trace("IoDriver: dropped registration for fd {}, refcount: {}", fmtFd(rio.fd()), newRegs);

    if (newRegs == 0) {
        trace("IoDriver: erasing entry for fd {}", fmtFd(rio.fd()));
        ios->erase(it);
    }
}

void IoDriver::vClearReadiness(IoDriver* self, IoEntry& rio, Interest interest) {
    ARC_ASSERT(interest != Interest::ReadWrite);

    trace("IoDriver: clearing readiness for fd {}, interest {}", fmtFd(rio.fd), static_cast<uint8_t>(interest));

    auto curr = rio.readiness.load(acquire);
    uint8_t newReady = curr & ~static_cast<uint8_t>(interest);
    rio.readiness.store(newReady, release);
}

Interest IoDriver::vPollReady(IoDriver* self, IoEntry& rio, Interest interest, Context& cx, uint64_t& outId) {
    // Always poll for error
    interest |= Interest::Error;

    auto curr = rio.readiness.load(acquire);

    trace("IoDriver: fd {} readiness: {}", fmtFd(rio.fd), curr);

    uint8_t readiness = (curr & static_cast<uint8_t>(interest));

    if (readiness != 0) {
        return readiness;
    }

    // lock the wait list
    auto waiters = rio.waiters.lock();

    // check for a lost wakeup now that we are holding the lock
    curr = rio.readiness.load(acquire);
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
            it->waker = cx.waker() ? std::make_optional(cx.cloneWaker()) : std::nullopt;
        }

        return 0;
    }

    outId = nextId();
    waiters->emplace_back(IoWaiter(cx.cloneWaker(), outId, interest));
    waiters.unlock();

    trace("IoDriver: added waiter for fd {}: interest {}", fmtFd(rio.fd), static_cast<uint8_t>(interest));

    if ((interest & Interest::Readable) != 0) {
        rio.anyRead.store(true, release);
    }

    if ((interest & Interest::Writable) != 0) {
        rio.anyWrite.store(true, release);
    }

    return 0;
}

void IoDriver::vUnregisterWaiter(IoDriver* self, IoEntry& rio, uint64_t id) {
    auto waiters = rio.waiters.lock();

    auto it = std::find_if(waiters->begin(), waiters->end(), [id](const IoWaiter& waiter) {
        return waiter.id == id;
    });

    if (it == waiters->end()) {
        return;
    }

    trace("IoDriver: removed waiter for fd {}, id {}", fmtFd(rio.fd), id);

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

    rio.anyRead.store(hasRead, release);
    rio.anyWrite.store(hasWrite, release);
}

SockFd IoDriver::vFdForEntry(const IoEntry& rio) {
    return rio.fd;
}

void IoDriver::doWork() {
    ARC_POLLFD fds[64];
    IoEntry* entries[64];
    int count = 0;

    auto ios = m_ios.lock();

    for (auto& [fd, rio] : *ios) {
        ARC_DEBUG_ASSERT(rio && fd == rio->fd);

        bool read = rio->anyRead.load(std::memory_order::relaxed);
        bool write = rio->anyWrite.load(std::memory_order::relaxed);

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

        // do nothing extra if there aren't any events
        if (pfd.revents == 0) continue;

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

        auto newReadiness = rio->readiness.fetch_or(ready, relaxed) | static_cast<uint8_t>(ready);
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
