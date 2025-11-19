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

bool Notified::pollImpl() {
    switch (m_waitState.load(acquire)) {
        case State::Init: {
            // register the waker

            m_waitState.store(State::Waiting, release);
            m_notify->m_waiters.add(*ctx().m_waker, this);
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

Notified Notify::notified() {
    return Notified{m_state};
}

void Notify::notifyOne() {
    if (auto w = m_state->m_waiters.takeFirst()) {
        notify(w->waker, w->awaiter);
    }
}

void Notify::notifyAll() {
    m_state->m_waiters.forAll([](Waker& waker, Notified* awaiter) {
        notify(waker, awaiter);
    });
}

Notify::Notify() : m_state(std::make_shared<NotifyState>()) {}

}