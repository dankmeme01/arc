#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <asp/sync/Mutex.hpp>
#include <asp/ptr/SharedPtr.hpp>
#include <atomic>

namespace arc {

struct Notified;

struct NotifyState {
    asp::Mutex<WaitList<Notified>> m_waiters;
    bool m_storedPermit = false; // also guarded by m_waiters

    bool claimStoredOrRegister(Notified* notified, Context& cx);
    void unregister(Notified* notified);
};

struct ARC_NODISCARD Notified : Pollable<Notified> {
    enum class State : uint8_t {
        Init,
        Waiting,
        Notified,
    };

    asp::SharedPtr<NotifyState> m_notify;
    std::atomic<State> m_waitState{State::Init};

    explicit Notified(asp::SharedPtr<NotifyState> state) : m_notify(std::move(state)) {}

    // Because atomic cannot be moved, we must manually define move constructors
    Notified(Notified&&) noexcept;
    Notified& operator=(Notified&&) noexcept = delete;

    ~Notified();

    bool poll(Context& cx);
    void reset();
};

/// Synchronization primitive that allows a task to wait for notifications, be it from another task or synchronous code.
/// Notify can safely be copied around, and it will still reference the same internal state.
class Notify {
public:
    Notify();

    Notify(const Notify& other) = default;
    Notify& operator=(const Notify& other) = default;

    /// Returns an awaitable future that completes when notified.
    Notified notified() const;

    /// Notifies one waiter. If no waiter is present, up to a single permit can be stored,
    /// and the next call to `notified()` will complete immediately. This does not happen if `store` is false.
    void notifyOne(bool store = true) const;

    /// Notifies all waiters, no permits are stored.
    void notifyAll() const;

private:
    asp::SharedPtr<NotifyState> m_state;

    void notify(Waker& waker, Notified* waiter) const;
};

}
