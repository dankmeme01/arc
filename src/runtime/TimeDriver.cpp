#include <arc/runtime/TimeDriver.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

TimeDriver::TimeDriver(Runtime* runtime) : m_runtime(runtime) {}

TimeDriver::~TimeDriver() {}

void TimeDriver::doWork() {
    std::array<std::optional<TimerEntry>, 64> readyHandles;
    size_t count = 0;

    auto now = Instant::now();

    auto timers = m_timers.lock();
    while (!timers->empty() && count < readyHandles.size()) {
        auto& entry = const_cast<TimerEntry&>(timers->top());
        if (entry.expiry > now) {
            break;
        }

        readyHandles[count++] = std::move(entry);
        timers->pop();
    }

    timers.unlock();

    for (size_t i = 0; i < count; i++) {
        readyHandles[i]->waker.wake();
    }
}

void TimeDriver::addEntry(Instant expiry, Waker waker) {
    m_timers.lock()->emplace(TimerEntry{expiry, std::move(waker)});
}

}