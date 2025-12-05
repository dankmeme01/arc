#include <arc/sync/Notify.hpp>
#include <arc/task/Context.hpp>
#include <utility>

using enum std::memory_order;

namespace arc {

static void notify(Waker& waker, Notified* waiter) {
    auto expected = Notified::State::Waiting;
    bool exchanged = waiter->m_waitState.compare_exchange_strong(expected, Notified::State::Notified, acq_rel, acquire);

    if (exchanged) {
        waker.wake();
    } else {
        // was already notified or in init state, do nothing
    }
}

Notified::~Notified() {
    this->reset();
}

void Notified::reset() {
    auto state = m_waitState.load(acquire);
    if (state == State::Waiting) {
        m_notify->m_waiters.remove(this);
    }

    m_waitState.store(State::Init, release);
}

Notified::Notified(Notified&& other) noexcept {
    *this = std::move(other);
}

Notified& Notified::operator=(Notified&& other) noexcept {
    this->reset();

    m_notify = std::move(other.m_notify);
    auto state = other.m_waitState.load(acquire);
    m_waitState.store(state, release);

    // was the other notify waiting? re-register as ourselves
    if (state == State::Waiting) {
        m_notify->m_waiters.swapData(&other, this);

        // one last check, in case the other notified gets notified in the middle of this
        if (!other.m_waitState.compare_exchange_strong(state, State::Init)) {
            // `other` got notified, reset it and bring the notification here
            m_waitState.store(state, release);
            other.m_waitState.store(State::Init, acquire);
        }
    }
    // were we already notified?
    else if (state == State::Notified) {
        other.m_waitState.store(State::Init, acquire);
    }

    return *this;
}

bool Notified::poll() {
    switch (m_waitState.load(acquire)) {
        case State::Init: {
            // try to consume a stored permit
            if (this->claimStored()) {
                return true;
            }

            // register the waker

            m_waitState.store(State::Waiting, release);
            m_notify->m_waiters.add(*ctx().m_waker, this);

            // double check for a stored permit
            if (this->claimStored()) {
                m_notify->m_waiters.remove(this);
                return true;
            }

            return false;
        } break;

        case State::Waiting: {
            if (this->claimStored()) {
                m_notify->m_waiters.remove(this);
                return true;
            }

            return false;
        } break;

        case State::Notified: {
            return true;
        } break;

        default: std::unreachable();
    }
}

bool Notified::claimStored() {
    bool expected = true;
    if (m_notify->m_storedPermit.compare_exchange_strong(expected, false, acq_rel, acquire)) {
        m_waitState.store(State::Notified, release);
        return true;
    }
    return false;
}

Notified Notify::notified() {
    return Notified{m_state};
}

void Notify::notifyOne(bool store) {
    if (auto w = m_state->m_waiters.takeFirst()) {
        notify(w->waker, w->awaiter);
    } else if (store) {
        m_state->m_storedPermit.store(true, release);
    }
}

void Notify::notifyAll() {
    m_state->m_waiters.forAll([](Waker& waker, Notified* awaiter) {
        notify(waker, awaiter);
    });
}

Notify::Notify() : m_state(std::make_shared<NotifyState>()) {}

}