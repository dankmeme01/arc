#include <arc/Sleep.hpp>
#include <arc/Runtime.hpp>

using namespace asp::time;

namespace arc {

bool Sleep::await_ready() const noexcept {
    // return m_expiry <= asp::time::Instant::now();
    return false;
}

void Sleep::await_suspend(std::coroutine_handle<> h) noexcept {
    g_runtime->timeDriver().addEntry(m_expiry, h);
}

void Sleep::await_resume() noexcept {}

Sleep sleepFor(asp::time::Duration duration) {
    return Sleep(Instant::now() + duration);
}

Sleep sleepUntil(asp::time::Instant expiry) {
    return Sleep(expiry);
}

}