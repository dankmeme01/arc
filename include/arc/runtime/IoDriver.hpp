#pragma once
#include <arc/task/Waker.hpp>
#include <qsox/BaseSocket.hpp>
#include <asp/sync/SpinLock.hpp>
#include <asp/sync/Mutex.hpp>
#include <std23/move_only_function.h>
#include <memory>
#include <vector>
#include <atomic>

namespace arc {

using SockFd = qsox::SockFd;

struct Interest {
    enum Type : uint8_t {
        Readable = 1 << 0,
        Writable = 1 << 1,
        Error = 1 << 2,
        ReadWrite = Readable | Writable,
    };

    Interest() : m_type{} {}
    Interest(Type type) : m_type(type) {}
    Interest(uint8_t type) : m_type(static_cast<Type>(type)) {}

    Interest operator|(const Interest& other) const {
        return Interest(static_cast<Type>(m_type | other.m_type));
    }

    Interest operator|(Type other) const {
        return Interest(static_cast<Type>(m_type | other));
    }

    Interest operator&(uint8_t other) const {
        return Interest(static_cast<Type>(m_type & other));
    }

    Interest& operator|=(const Interest& other) {
        m_type = static_cast<Type>(m_type | other.m_type);
        return *this;
    }

    Interest& operator|=(uint8_t other) {
        m_type = static_cast<Type>(m_type | other);
        return *this;
    }

    operator uint8_t() const {
        return static_cast<uint8_t>(m_type);
    }

private:
    Type m_type{};
};

class Runtime;
class IoDriver;

struct IoWaiter {
    IoWaiter(Waker waker, uint64_t id, Interest interest);
    IoWaiter(std23::move_only_function<void()> eventCallback, uint64_t id, Interest interest);

    bool willWake(const Waker& other) const;
    bool satisfiedBy(Interest ready) const;
    void wake();

    uint64_t getId() const { return id; }

private:
    friend class Registration;

    std::optional<Waker> waker;
    uint64_t id;
    std23::move_only_function<void()> eventCallback;
    Interest interest;
};

struct IoEntry {
    SockFd fd;
    asp::SpinLock<std::vector<IoWaiter>> waiters; // TODO: slab kind of thing
    std::atomic<bool> anyWrite{false}, anyRead{false};
    std::atomic<uint8_t> readiness{0};
    std::weak_ptr<Runtime> runtime;
};

struct Registration {
    std::shared_ptr<IoEntry> rio;

    Registration(std::shared_ptr<IoEntry> rio);
    Registration(Registration&& other) noexcept = default;
    Registration& operator=(Registration&& other) noexcept = default;
    Registration(const Registration&) = default;
    Registration& operator=(const Registration&) = default;

    Interest pollReady(Interest interest, uint64_t& outId);
    void unregister(uint64_t id);
    void clearReadiness(Interest interest);
};

class IoDriver {
public:
    IoDriver(std::weak_ptr<Runtime> runtime);
    ~IoDriver();

    Registration registerIo(SockFd fd, Interest interest);
    void unregisterIo(const Registration& rio);
    void unregisterIo(SockFd fd);

private:
    friend class Runtime;

    std::weak_ptr<Runtime> m_runtime;
    std::atomic<uint64_t> m_tick{0};
    asp::Mutex<std::unordered_map<SockFd, std::shared_ptr<IoEntry>>> m_ios;
    asp::SpinLock<std::vector<std::shared_ptr<IoEntry>>> m_ioPendingQueue;

    void doWork();
};

}