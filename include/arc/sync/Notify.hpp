#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <memory>
#include <atomic>

namespace arc {

struct Notified;

struct NotifyState {
    WaitList<Notified> m_waiters;
};

struct Notified : PollableBase<Notified, void> {
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

    bool pollImpl();
    void reset();
};

class Notify {
public:
    Notify();

    Notified notified();
    void notifyOne();
    void notifyAll();

private:
    std::shared_ptr<NotifyState> m_state;
};

}
