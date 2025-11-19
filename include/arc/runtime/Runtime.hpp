#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <unordered_set>
#include <cstddef>
#include <condition_variable>
#include <arc/task/Task.hpp>

#include <asp/time/Duration.hpp>
#include <asp/time/sleep.hpp>
#include "TimeDriver.hpp"
#include "SignalDriver.hpp"

namespace arc {

struct Runtime {
    Runtime(size_t workers = std::thread::hardware_concurrency());
    ~Runtime();

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

        std::condition_variable cv;

        // Create a waker that will notify us
        static constexpr RawWakerVtable vtable = {
            .wake = [](void* data) {
                static_cast<std::condition_variable*>(data)->notify_one();
            },
            .wakeByRef = [](void* data) {
                static_cast<std::condition_variable*>(data)->notify_one();
            },
            .clone = [](void* data) -> RawWaker {
                return RawWaker{data, &vtable};
            },
            .destroy = [](void*) {},
        };
        auto cvWaker = Waker{&cv, &vtable};

        handle.m_task->registerAwaiter(cvWaker);

        while (true) {
            auto result = handle.pollTask();
            if (result) {
                if constexpr (!std::is_void_v<T>) {
                    return std::move(*result);
                } else {
                    return;
                }
            }

            // wait
            std::mutex m;
            std::unique_lock lock(m);
            cv.wait(lock);
        }
    }

private:
    friend class Runtime;

    std::atomic<bool> m_stopFlag;
    std::mutex m_mtx;
    std::unordered_set<TaskBase*> m_tasks;
    std::deque<TaskBase*> m_runQueue;
    std::condition_variable m_cv;
    std::vector<std::thread> m_workers;
    TimeDriver m_timeDriver{this};
    SignalDriver m_signalDriver{this};

    void shutdown();

    void workerLoop(size_t id);
    void timerDriverLoop();
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