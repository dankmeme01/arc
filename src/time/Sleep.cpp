#include <arc/time/Sleep.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

bool Sleep::pollImpl() {
    auto now = Instant::now();
    if (now >= m_expiry) {
        return true;
    } else {
        ctx().runtime()->timeDriver().addEntry(m_expiry, ctx().m_waker->clone());
        return false;
    }
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