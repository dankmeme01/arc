#include <arc/sync/Latch.hpp>
#include <arc/util/Assert.hpp>

using enum std::memory_order;

namespace arc {

bool Latch::Awaiter::poll(Context& cx) {
    switch (m_state) {
        case None: {
            // not yet registered, first do a fast check for completed
            if (m_latch.m_count.load(::acquire) == 0) {
                m_state = Completed;
                return true;
            }

            // otherwise, register
            m_latch.m_waiters.lock()->add(cx.cloneWaker(), this);

            // check if the state changed again, to avoid a data race where the latch was released between the first check and the registration
            if (m_latch.m_count.load(::acquire) == 0) {
                m_latch.m_waiters.lock()->remove(this);
                m_state = Completed;
                return true;
            }

            // otherwise, we wait
            m_state = Registered;
            return false;
        } break;

        case Registered: {
            // check if fulfilled, no need to remove self from waiters since the latch will do that
            if (m_latch.m_count.load(::acquire) == 0) {
                m_state = Completed;
                return true;
            }
            return false;
        } break;

        case Completed: break;
    }

    std::unreachable();
}

Latch::Awaiter::Awaiter(Awaiter&& other) noexcept
    : m_latch(other.m_latch)
{
    ARC_ASSERT(other.m_state == None, "cannot move a Awaiter that already was polled");
}

Latch::Awaiter::~Awaiter() {
    if (m_state == Registered) {
        m_latch.m_waiters.lock()->remove(this);
    }
}

Latch::Awaiter Latch::wait() noexcept {
    return Awaiter{*this};
}

void Latch::countDown(size_t n) {
    auto expected = m_count.load(::acquire);
    while (true) {
        if (expected == 0) {
            return;
        }

        size_t newCount = expected - std::min(expected, n);
        if (m_count.compare_exchange_weak(expected, newCount, ::acq_rel, ::acquire)) {
            if (newCount == 0) {
                this->release();
            }
            return;
        }
    }
}

void Latch::release() {
    auto waiters = m_waiters.lock();
    waiters->forAll([](Waker& waker, Awaiter* awaiter) {
        waker.wake();
    });
}

}
