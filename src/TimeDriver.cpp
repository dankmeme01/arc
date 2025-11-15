#include <arc/TimeDriver.hpp>
#include <arc/Runtime.hpp>

using namespace asp::time;

namespace arc {

TimeDriver::TimeDriver(Runtime* runtime) : m_runtime(runtime) {}

TimeDriver::~TimeDriver() {}

void TimeDriver::doWork() {
    std::array<std::coroutine_handle<>, 64> readyHandles;
    size_t count = 0;

    auto now = Instant::now();

    auto timers = m_timers.lock();
    while (!timers->empty() && count < readyHandles.size()) {
        auto& entry = timers->top();
        if (entry.expiry > now) {
            break;
        }

        readyHandles[count++] = entry.handle;
        timers->pop();
    }

    timers.unlock();

    for (size_t i = 0; i < count; i++) {
        m_runtime->enqueue(readyHandles[i]);
    }
}

void TimeDriver::addEntry(Instant expiry, std::coroutine_handle<> handle) {
    m_timers.lock()->emplace(TimerEntry{expiry, handle});
}

void TimeDriver::addTimeout(Duration delay, std::coroutine_handle<> handle) {
    this->addEntry(Instant::now() + delay, handle);
}

}