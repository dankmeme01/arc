#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <unordered_set>
#include <cstddef>
#include <condition_variable>
#include <arc/task/Task.hpp>
#include <arc/task/BlockingTask.hpp>
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
        m_tasks.lock()->insert(task);
        task->schedule();

        return TaskHandle<T>{task};
    }

    template <typename T>
    BlockingTaskHandle<T> spawnBlocking(std23::move_only_function<T()> func) {
        BlockingTaskHandle<T> handle{ BlockingTask<T>::create(this, std::move(func)) };

        std::unique_lock lock(m_blockingMtx);
        auto& btasks = m_blockingTasks;
        btasks.push_back(handle.m_task.get());
        this->ensureBlockingWorker(btasks.size());
        m_blockingCv.notify_one();

        return handle;
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

    std::atomic<bool> m_stopFlag{false};
    TimeDriver m_timeDriver{this};
    SignalDriver m_signalDriver{this};
    IoDriver m_ioDriver{this};

    std::mutex m_mtx;
    asp::SpinLock<std::unordered_set<TaskBase*>> m_tasks;
    std::deque<TaskBase*> m_runQueue; // protected by m_mtx
    std::vector<WorkerData> m_workers;
    std::condition_variable m_cv;

    std::mutex m_blockingMtx;
    std::deque<BlockingTaskBase*> m_blockingTasks; // protected by m_blockingMtx
    std::condition_variable m_blockingCv;
    std::atomic<size_t> m_blockingWorkers{0};
    std::atomic<size_t> m_freeBlockingWorkers{0};
    std::atomic<size_t> m_nextBlockingWorkerId{0};

    void shutdown();

    void workerLoop(WorkerData& data);
    void workerLoopWrapper(WorkerData& data);
    void blockingWorkerLoop(size_t id);

    void removeTask(TaskBase* task) noexcept;
    void ensureBlockingWorker(size_t tasksInQueue);
    void spawnBlockingWorker();
};

template <Pollable F>
auto spawn(F t) {
    if (auto rt = ctx().runtime()) {
        return rt->spawn(std::move(t));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

/// Spawns a blocking function onto a thread pool.
/// Use this when you need to run expensive synchronous code inside an async context.
/// The returned handle can be awaited to get the result of the function.
template <typename T>
auto spawnBlocking(std23::move_only_function<T()> f) {
    if (auto rt = ctx().runtime()) {
        return rt->spawnBlocking<T>(std::move(f));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

}
