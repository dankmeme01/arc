#include <arc/runtime/TimeDriver.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

TimeDriver::TimeDriver(std::weak_ptr<Runtime> runtime) : m_runtime(std::move(runtime)) {}

TimeDriver::~TimeDriver() {}

void TimeDriver::doWork() {
    std::array<std::optional<Waker>, 64> readyHandles;
    size_t count = 0;

    auto now = Instant::now();

    auto timers = m_timers.lock();
    auto it = timers->begin();
    while (it != timers->end() && count < readyHandles.size()) {
        if (it->first.expiry > now) {
            break;
        }

        readyHandles[count++] = std::move(it->second);
        it = timers->erase(it);
    }

    timers.unlock();

    for (size_t i = 0; i < count; i++) {
        readyHandles[i]->wake();
    }
}

uint64_t TimeDriver::addEntry(Instant expiry, Waker waker) {
    uint64_t id = m_nextTimerId.fetch_add(1, std::memory_order::relaxed);
    m_timers.lock()->emplace(TimerEntryKey{expiry, id}, std::move(waker));
    return id;
}

void TimeDriver::removeEntry(Instant expiry, uint64_t id) {
    auto rt = m_runtime.lock();
    if (!rt || rt->isShuttingDown()) return;

    TimerEntryKey key{expiry, id};

    auto timers = m_timers.lock();
    auto it = timers->find(key);
    if (it != timers->end()) {
        timers->erase(it);
    }
}

}