#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_TIME
ARC_FATAL_NO_FEATURE(time)
#else

#include <arc/task/Task.hpp>
#include <asp/time/Instant.hpp>
#include <asp/sync/SpinLock.hpp>
#include <asp/collections/SmallVec.hpp>
#include <vector>

namespace arc {

class Runtime;

class TimeDriver;
struct TimeDriverVtable {
    using AddEntryFn = uint64_t(*)(TimeDriver*, asp::time::Instant, Waker);
    using RemoveEntryFn = void(*)(TimeDriver*, asp::time::Instant, uint64_t);

    AddEntryFn m_addEntry;
    RemoveEntryFn m_removeEntry;
};

struct TimerEntry {
    asp::Instant expiry;
    Waker waker;
    uint64_t id;

    bool operator>(const TimerEntry& other) const {
        if (expiry == other.expiry) return id > other.id;
        return expiry > other.expiry;
    }
};

struct TimerQueue {
    asp::SmallVec<TimerEntry, 32> drain();
    void insert(TimerEntry&&);
    void erase(const asp::Instant& expiry, uint64_t id);

private:
    std::vector<TimerEntry> m_entries;
};

class TimeDriver {
public:
    TimeDriver(asp::WeakPtr<Runtime> runtime);
    TimeDriver(const TimeDriver&) = delete;
    TimeDriver& operator=(const TimeDriver&) = delete;
    ~TimeDriver();

    uint64_t addEntry(asp::time::Instant expiry, Waker waker);
    void removeEntry(asp::time::Instant expiry, uint64_t id);

private:
    friend class Runtime;

    const TimeDriverVtable* m_vtable;
    std::atomic<uint64_t> m_nextTimerId{1};
    asp::WeakPtr<Runtime> m_runtime;
    asp::SpinLock<TimerQueue> m_timers;

    void doWork();

    static uint64_t vAddEntry(TimeDriver* self, asp::Instant expiry, Waker waker);
    static void vRemoveEntry(TimeDriver* self, asp::Instant expiry, uint64_t id);
};

}

#endif
