#pragma once

#include <arc/task/Task.hpp>
#include <asp/time/Instant.hpp>
#include <asp/sync/SpinLock.hpp>
#include <queue>

namespace arc {

class Runtime;

struct TimerEntry {
    asp::time::Instant expiry;
    Waker waker;

    bool operator>(const TimerEntry& other) const {
        return expiry > other.expiry;
    }
};

using TimerQueue = std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<>>;

class TimeDriver {
public:
    TimeDriver(Runtime* runtime);
    ~TimeDriver();

    void addEntry(asp::time::Instant expiry, Waker waker);

private:
    friend struct Runtime;

    Runtime* m_runtime;
    asp::SpinLock<TimerQueue> m_timers;

    void doWork();
};

}