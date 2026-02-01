#pragma once
#include <arc/task/Waker.hpp>
#include <qsox/BaseSocket.hpp>
#include <asp/sync/SpinLock.hpp>
#include <asp/sync/Mutex.hpp>
#include <asp/ptr/SharedPtr.hpp>
#include <arc/future/Context.hpp>
#include <arc/util/Function.hpp>
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
    IoWaiter(arc::MoveOnlyFunction<void()> eventCallback, uint64_t id, Interest interest);

    IoWaiter(IoWaiter&&) noexcept = default;
    IoWaiter& operator=(IoWaiter&&) noexcept = default;
    IoWaiter(const IoWaiter&) = delete;
    IoWaiter& operator=(const IoWaiter&) = delete;

    bool willWake(const Waker& other) const;
    bool satisfiedBy(Interest ready) const;
    void wake();

    uint64_t getId() const { return id; }

private:
    friend class IoDriver;

    std::optional<Waker> waker;
    uint64_t id;
    arc::MoveOnlyFunction<void()> eventCallback;
    Interest interest;
};

struct IoEntry {
    SockFd fd;
    asp::SpinLock<std::vector<IoWaiter>> waiters; // TODO: slab kind of thing
    std::atomic<bool> anyWrite{false}, anyRead{false};
    std::atomic<uint8_t> readiness{0};
    std::atomic<size_t> registrations{1};
    asp::WeakPtr<Runtime> runtime;
};

/// Opaque registration handle for an IO resource
struct Registration {
    Registration(asp::SharedPtr<IoEntry> rio, IoDriver* driver);
    Registration(Registration&& other) noexcept = default;
    Registration& operator=(Registration&& other) noexcept = default;
    Registration(const Registration&) = default;
    Registration& operator=(const Registration&) = default;
    ~Registration();

    operator bool() const;

    /// Polls the IO for readiness, if not ready then registers the task, cloning the waker,
    /// and returning the registration ID. If outId is set to a non-zero value, you must call `unregister`.
    /// Once woken up, you must call `unregister` or `pollReady` again to be woken up again.
    Interest pollReady(Interest interest, Context& cx, uint64_t& outId);
    void unregister(uint64_t id);
    void clearReadiness(Interest interest);
    SockFd fd() const;

    /// Nullifies this registration, removing the IO source from the driver if no more registrations exist for the same source.
    void reset();

private:
    friend class IoDriver;
    asp::SharedPtr<IoEntry> m_rio;
    IoDriver* m_driver;
};

class IoDriver;
struct IoDriverVtable {
    using RegisterIoFn = Registration(*)(IoDriver*, SockFd, Interest);
    using DropRegistrationFn = void(*)(IoDriver*, const Registration&);
    using ClearReadinessFn = void(*)(IoDriver*, IoEntry&, Interest);
    using PollReadyFn = Interest(*)(IoDriver*, IoEntry&, Interest, Context&, uint64_t&);
    using UnregisterWaiterFn = void(*)(IoDriver*, IoEntry&, uint64_t);
    using FdForEntryFn = SockFd(*)(const IoEntry&);

    RegisterIoFn m_registerIo;
    DropRegistrationFn m_dropRegistration;
    ClearReadinessFn m_clearReadiness;
    PollReadyFn m_pollReady;
    UnregisterWaiterFn m_unregisterWaiter;
    FdForEntryFn m_fdForEntry;
};

class IoDriver {
public:
    IoDriver(asp::WeakPtr<Runtime> runtime);
    ~IoDriver();

    Registration registerIo(SockFd fd, Interest interest);
    void dropRegistration(const Registration& rio);

    void clearReadiness(IoEntry& rio, Interest interest);
    Interest pollReady(IoEntry& rio, Interest interest, Context& cx, uint64_t& outId);
    void unregisterWaiter(IoEntry& rio, uint64_t id);
    SockFd fdForEntry(const IoEntry& rio);

private:
    friend class Runtime;

    const IoDriverVtable* m_vtable;
    asp::WeakPtr<Runtime> m_runtime;
    std::atomic<uint64_t> m_tick{0};
    asp::Mutex<std::unordered_map<SockFd, asp::SharedPtr<IoEntry>>> m_ios;

    void doWork();

    static Registration vRegisterIo(IoDriver* self, SockFd fd, Interest interest);
    static void vDropRegistration(IoDriver* self, const Registration& rio);
    static void vClearReadiness(IoDriver* self, IoEntry& rio, Interest interest);
    static Interest vPollReady(IoDriver* self, IoEntry& rio, Interest interest, Context& cx, uint64_t& outId);
    static void vUnregisterWaiter(IoDriver* self, IoEntry& rio, uint64_t id);
    static SockFd vFdForEntry(const IoEntry& rio);
};

}