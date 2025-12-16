#include <arc/time/Sleep.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Trace.hpp>

using namespace asp::time;

namespace arc {

bool Sleep::poll() {
    if (ctx().shouldCoopYield()) {
        return false;
    }

    auto now = Instant::now();
    if (now >= m_expiry) {
        m_id = 0;
        return true;
    } else {
        // only register if we aren't already registered
        if (m_id == 0) {
            m_id = ctx().runtime()->timeDriver().addEntry(m_expiry, ctx().cloneWaker());
        }

        return false;
    }
}

Sleep::~Sleep() {
    if (m_id != 0) {
        ctx().runtime()->timeDriver().removeEntry(m_expiry, m_id);
    }
}

Sleep::Sleep(Sleep&& other) noexcept {
    *this = std::move(other);
}

Sleep& Sleep::operator=(Sleep&& other) noexcept {
    if (this != &other) {
        m_expiry = other.m_expiry;
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

Sleep sleep(asp::time::Duration duration) {
    return sleepFor(duration);
}

Sleep sleepFor(asp::time::Duration duration) {
    return Sleep(Instant::now() + duration);
}

Sleep sleepUntil(asp::time::Instant expiry) {
    return Sleep(expiry);
}

}