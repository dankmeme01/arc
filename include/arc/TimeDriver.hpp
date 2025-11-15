#pragma once

#include "Task.hpp"
#include <asp/time/Instant.hpp>
#include <asp/sync/SpinLock.hpp>
#include <queue>

namespace arc {

class Runtime;

struct TimerEntry {
    asp::time::Instant expiry;
    std::coroutine_handle<> handle;

    bool operator>(const TimerEntry& other) const {
        return expiry > other.expiry;
    }
};

using TimerQueue = std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<>>;

class TimeDriver {
public:
    TimeDriver(Runtime* runtime);
    ~TimeDriver();

    void addEntry(asp::time::Instant expiry, std::coroutine_handle<> handle);
    void addTimeout(asp::time::Duration delay, std::coroutine_handle<> handle);

private:
    friend struct Runtime;

    Runtime* m_runtime;
    asp::SpinLock<TimerQueue> m_timers;

    void doWork();
};

}