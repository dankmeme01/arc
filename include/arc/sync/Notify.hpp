#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <memory>
#include <atomic>

namespace arc {

struct Notified;

struct NotifyState {
    WaitList<Notified> m_waiters;
    std::atomic<bool> m_storedPermit{false};
};

struct ARC_NODISCARD Notified : PollableBase<Notified> {
    enum class State : uint8_t {
        Init,
        Waiting,
        Notified,
    };

    std::shared_ptr<NotifyState> m_notify;
    std::atomic<State> m_waitState{State::Init};

    explicit Notified(std::shared_ptr<NotifyState> state) : m_notify(std::move(state)) {}

    // Because atomic cannot be moved, we must manually define move constructors
    Notified(Notified&&) noexcept;
    Notified& operator=(Notified&&) noexcept;

    ~Notified();

    bool poll();
    void reset();
    bool claimStored();
};

class Notify {
public:
    Notify();

    /// Returns an awaitable future that completes when notified.
    Notified notified() const;

    /// Notifies one waiter. If no waiter is present, up to a single permit can be stored,
    /// and the next call to `notified()` will complete immediately. This does not happen if `store` is false.
    void notifyOne(bool store = true) const;

    /// Notifies all waiters, no permits are stored.
    void notifyAll() const;

private:
    std::shared_ptr<NotifyState> m_state;
};

}
