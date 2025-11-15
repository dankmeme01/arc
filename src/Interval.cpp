#include <arc/Interval.hpp>
#include <arc/Runtime.hpp>

using namespace asp::time;

namespace arc {

Interval::Interval(Duration period)
    : m_current(Instant::now()),
      m_period(period) {}

bool Interval::await_ready() const noexcept {
    return false;
}

void Interval::await_suspend(std::coroutine_handle<> h) noexcept {
    g_runtime->timeDriver().addEntry(m_current, h);
}

void Interval::await_resume() noexcept {
    m_current += m_period;

    // if we are behind and skip is enabled, skip until the next future tick
    if (m_mtBehavior == MissedTickBehavior::Skip) {
        auto now = Instant::now();
        while (m_current <= now) {
            m_current += m_period;
        }
    }
}

void Interval::setMissedTickBehavior(MissedTickBehavior behavior) {
    m_mtBehavior = behavior;
}

Interval interval(asp::time::Duration period) {
    return Interval(period);
}

}