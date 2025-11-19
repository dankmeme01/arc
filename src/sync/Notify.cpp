#include <arc/sync/Notify.hpp>
#include <arc/task/Context.hpp>
#include <utility>

using enum std::memory_order;

namespace arc {

static void notify(Waker& waker, Notified* waiter) {
    waiter->m_waitState.store(Notified::State::Notified, release);
    waker.wake(); // consume waker
}

Notified::~Notified() {
    this->reset();
}

void Notified::reset() {
    if (m_waitState.load(acquire) == State::Waiting) {
        m_notify->m_waiters.remove(this);
        m_waitState.store(State::Init, release);
    }
}

Notified::Notified(Notified&& other) noexcept {
    *this = std::move(other);
}

Notified& Notified::operator=(Notified&& other) noexcept {
    this->reset();

    m_notify = std::move(other.m_notify);
    auto state = other.m_waitState.load(acquire);
    other.m_waitState.store(State::Init, release);
    m_waitState.store(state, release);

    // were we waiting? re-register ourselves
    if (state == State::Waiting) {
        m_notify->m_waiters.swapData(&other, this);
    }

    return *this;
}

bool Notified::pollImpl() {
    switch (m_waitState.load(acquire)) {
        case State::Init: {
            // register the waker

            m_notify->m_waiters.add(*ctx().m_waker, this);
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