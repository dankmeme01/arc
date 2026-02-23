#include <arc/runtime/TimeDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <array>

using namespace asp::time;

namespace arc {

// TimerQueue sorts in reverse, so soonest expiry is at the end, for faster removal
asp::SmallVec<TimerEntry, 32> TimerQueue::drain() {
    asp::SmallVec<TimerEntry, 32> out;
    auto now = Instant::now();

    TimerEntry dummy { now, {}, UINT64_MAX };

    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), dummy, std::greater<TimerEntry>{});
    // 'it' and all elements after it are either in the past or equal to 'now'

    size_t count = std::distance(it, m_entries.end());
    out.reserve(count);
    for (auto jt = it; jt != m_entries.end(); jt++) {
        out.emplace_back(std::move(*jt));
    }
    m_entries.erase(it, m_entries.end());

    return out;
}

void TimerQueue::insert(TimerEntry&& entry) {
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), entry, std::greater<TimerEntry>{});
    m_entries.insert(it, std::move(entry));
}

void TimerQueue::erase(const asp::Instant& expiry, uint64_t id) {
    TimerEntry dummy { expiry, {}, id };
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), dummy, std::greater<TimerEntry>{});
    if (it != m_entries.end() && it->id == id) {
        m_entries.erase(it);
    }
}

TimeDriver::TimeDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    static constexpr TimeDriverVtable vtable{
        .m_addEntry = &TimeDriver::vAddEntry,
        .m_removeEntry = &TimeDriver::vRemoveEntry,
    };
    m_vtable = &vtable;
}

TimeDriver::~TimeDriver() {}

void TimeDriver::doWork() {
    auto readyHandles = m_timers.lock()->drain();
    for (auto& entry : readyHandles) {
        entry.waker.wake();
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
    self->m_timers.lock()->insert(TimerEntry{expiry, std::move(waker), id});
    return id;
}

void TimeDriver::vRemoveEntry(TimeDriver* self, Instant expiry, uint64_t id) {
    auto rt = self->m_runtime.upgrade();
    if (!rt || rt->isShuttingDown()) return;

    self->m_timers.lock()->erase(expiry, id);
}

}