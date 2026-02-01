#include <arc/runtime/TimeDriver.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

namespace arc {

TimeDriver::TimeDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    static const TimeDriverVtable vtable{
        .m_addEntry = &TimeDriver::vAddEntry,
        .m_removeEntry = &TimeDriver::vRemoveEntry,
    };
    m_vtable = &vtable;
}

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

uint64_t TimeDriver::addEntry(asp::time::Instant expiry, Waker waker) {
    return m_vtable->m_addEntry(this, expiry, std::move(waker));
}

void TimeDriver::removeEntry(asp::time::Instant expiry, uint64_t id) {
    m_vtable->m_removeEntry(this, expiry, id);
}

uint64_t TimeDriver::vAddEntry(TimeDriver* self, Instant expiry, Waker waker) {
    uint64_t id = self->m_nextTimerId.fetch_add(1, std::memory_order::relaxed);
    self->m_timers.lock()->emplace(TimerEntryKey{expiry, id}, std::move(waker));
    return id;
}

void TimeDriver::vRemoveEntry(TimeDriver* self, Instant expiry, uint64_t id) {
    auto rt = self->m_runtime.upgrade();
    if (!rt || rt->isShuttingDown()) return;

    TimerEntryKey key{expiry, id};

    auto timers = self->m_timers.lock();
    auto it = timers->find(key);
    if (it != timers->end()) {
        timers->erase(it);
    }
}

}