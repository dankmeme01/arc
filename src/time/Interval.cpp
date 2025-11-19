#include <arc/time/Interval.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

Interval::Interval(Duration period)
    : m_current(Instant::now()),
      m_period(period) {}


bool Interval::pollImpl() {
    auto& driver = ctx().runtime()->timeDriver();
    auto now = Instant::now();

    if (now < m_current) {
        driver.addEntry(m_current, ctx().m_waker->clone());
        return false;
    }

    // done!
    m_current += m_period;

    // if we are behind and skip is enabled, skip until the next future tick
    if (m_mtBehavior == MissedTickBehavior::Skip) {
        while (m_current <= now) {
            m_current += m_period;
        }
    }

    return true;
}

void Interval::setMissedTickBehavior(MissedTickBehavior behavior) {
    m_mtBehavior = behavior;
}

Interval interval(asp::time::Duration period) {
    return Interval(period);
}

}