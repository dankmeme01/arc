#include <arc/time/Interval.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

using Awaiter = Interval::Awaiter;

// Awaiter

bool Awaiter::poll(Context& cx) noexcept {
    return m_interval->doPoll(cx);
}

// Interval

Interval::Interval(Duration period) noexcept
    : m_current(Instant::now()),
      m_period(period) {}

Interval::~Interval() {
    if (m_id != 0) {
        auto rt = m_runtime.upgrade();
        if (rt && !rt->isShuttingDown()) {
            rt->timeDriver().removeEntry(m_current, m_id);
        }
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

bool Interval::doPoll(Context& cx) noexcept {
    m_runtime = cx.runtime()->weakFromThis();
    auto& driver = cx.runtime()->timeDriver();
    auto now = Instant::now();

    if (now < m_current) {
        if (m_id == 0) {
            m_id = driver.addEntry(m_current, cx.cloneWaker());
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

Awaiter Interval::tick() noexcept {
    return Awaiter{this};
}

Interval interval(asp::time::Duration period) noexcept {
    return Interval(period);
}

}