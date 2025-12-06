#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <unordered_set>
#include <cstddef>
#include <condition_variable>
#include <arc/task/Task.hpp>
#include <arc/task/CondvarWaker.hpp>

#include <asp/time/Duration.hpp>
#include <asp/time/sleep.hpp>
#include "TimeDriver.hpp"
#include "SignalDriver.hpp"
#include "IoDriver.hpp"

namespace arc {

class Runtime {
public:
    Runtime(size_t workers = std::thread::hardware_concurrency());
    ~Runtime();

    /// Get the thread-local runtime context, returns nullptr if not inside a runtime.
    static Runtime* current();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    // pin the runtime to a specific location in memory
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    inline TimeDriver& timeDriver() noexcept {
        return m_timeDriver;
    }

    inline SignalDriver& signalDriver() noexcept {
        return m_signalDriver;
    }

    inline IoDriver& ioDriver() noexcept {
        return m_ioDriver;
    }

    void enqueueTask(TaskBase* task);

    template <Pollable F, typename T = typename F::Output>
    TaskHandle<T> spawn(F fut) {
        auto* task = Task<F>::create(this, std::move(fut));
        m_tasks.insert(task);
        task->schedule();

        return TaskHandle<T>{task};
    }

    template <Pollable F, typename T = typename F::Output>
    T blockOn(F fut) {
        auto handle = this->spawn(std::move(fut));
        return handle.blockOn();
    }

private:
    template <Pollable P>
    friend struct Task;

    struct WorkerData {
        std::thread thread;
        size_t id;
    };

    std::atomic<bool> m_stopFlag;
    std::mutex m_mtx;
    std::unordered_set<TaskBase*> m_tasks;
    std::deque<TaskBase*> m_runQueue;
    std::condition_variable m_cv;
    std::vector<WorkerData> m_workers;
    TimeDriver m_timeDriver{this};
    SignalDriver m_signalDriver{this};
    IoDriver m_ioDriver{this};

    void shutdown();

    void workerLoop(WorkerData& data);
    void timerDriverLoop();
    void ioDriverLoop();

    void removeTask(TaskBase* task) noexcept;
};

template <Pollable F>
auto spawn(F t) {
    if (auto rt = ctx().runtime()) {
        return rt->spawn(std::move(t));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

}
