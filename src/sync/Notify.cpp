#include <arc/sync/Notify.hpp>
#include <arc/task/Context.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <utility>

using enum std::memory_order;

namespace arc {

bool NotifyState::claimStoredOrRegister(Notified* notified) {
    auto waiters = m_waiters.lock();
    if (m_storedPermit) {
        m_storedPermit = false;
        return true;
    }

    waiters->add(ctx().cloneWaker(), notified);
    return false;
}

void NotifyState::unregister(Notified* notified) {
    m_waiters.lock()->remove(notified);
}

Notified::~Notified() {
    this->reset();
}

void Notified::reset() {
    auto state = m_waitState.load(acquire);
    if (state == State::Waiting) {
        m_notify->unregister(this);
    }

    m_waitState.store(State::Init, release);
}

Notified::Notified(Notified&& other) noexcept {
    m_notify = std::move(other.m_notify);
    auto state = other.m_waitState.load(acquire);
    m_waitState.store(state, release);

    ARC_ASSERT(state != State::Waiting, "cannot move a Notified that is already waiting");
}

bool Notified::poll() {
    switch (m_waitState.load(acquire)) {
        case State::Init: {
            // try to consume a stored permit and register otherwise
            if (m_notify->claimStoredOrRegister(this)) {
                return true;
            }

            m_waitState.store(State::Waiting, release);
            return false;
        } break;

        case State::Waiting: {
            return false;
        } break;

        case State::Notified: {
            return true;
        } break;

        default: std::unreachable();
    }
}

Notified Notify::notified() const {
    return Notified{m_state};
}

void Notify::notifyOne(bool store) const {
    auto waiters = m_state->m_waiters.lock();

    if (auto w = waiters->takeFirst()) {
        this->notify(w->waker, w->awaiter);
    } else if (store) {
        m_state->m_storedPermit = true;
    }
}

void Notify::notifyAll() const {
    auto waiters = m_state->m_waiters.lock();
    waiters->forAll([this](Waker& waker, Notified* awaiter) {
        this->notify(waker, awaiter);
    });
}

void Notify::notify(Waker& waker, Notified* waiter) const {
    auto expected = Notified::State::Waiting;
    bool exchanged = waiter->m_waitState.compare_exchange_strong(expected, Notified::State::Notified, acq_rel, acquire);

    if (exchanged) {
        waker.wake();
    } else {
        // was already notified or in init state, do nothing
    }
}

Notify::Notify() : m_state(std::make_shared<NotifyState>()) {}

}