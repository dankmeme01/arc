#pragma once

#include <arc/task/Task.hpp>
#include <asp/time/Instant.hpp>
#include <asp/sync/SpinLock.hpp>
#include <map>

namespace arc {

class Runtime;

struct TimerEntryKey {
    asp::time::Instant expiry;
    uint64_t id;

    bool operator<(const TimerEntryKey& other) const {
        return expiry < other.expiry;
    }
};

using TimerQueue = std::map<TimerEntryKey, Waker, std::less<TimerEntryKey>>;

class TimeDriver {
public:
    TimeDriver(Runtime* runtime);
    ~TimeDriver();

    uint64_t addEntry(asp::time::Instant expiry, Waker waker);
    void removeEntry(asp::time::Instant expiry, uint64_t id);

private:
    friend struct Runtime;

    std::atomic<uint64_t> m_nextTimerId{1};
    Runtime* m_runtime;
    asp::SpinLock<TimerQueue> m_timers;

    void doWork();
};

}