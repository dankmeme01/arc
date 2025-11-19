#include <arc/time/Interval.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

Interval::Interval(Duration period)
    : m_current(Instant::now()),
      m_period(period) {}

Interval::~Interval() {
    if (m_id != 0) {
        ctx().runtime()->timeDriver().removeEntry(m_current, m_id);
    }
}

Interval::Interval(Interval&& other) noexcept {
    *this = std::move(other);
}

Interval& Interval::operator=(Interval&& other) noexcept {
    if (this != &other) {
        m_current = other.m_current;
        m_mtBehavior = other.m_mtBehavior;
        m_period = other.m_period;
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

bool Interval::pollImpl() {
    auto& driver = ctx().runtime()->timeDriver();
    auto now = Instant::now();

    if (now < m_current) {
        if (m_id == 0) {
            m_id = driver.addEntry(m_current, ctx().m_waker->clone());
        }
        return false;
    }

    // done!
    m_current += m_period;
    m_id = 0;

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