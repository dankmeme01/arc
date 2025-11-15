#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <cstddef>
#include <condition_variable>
#include "Task.hpp"
#include "TimeDriver.hpp"

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

    void enqueue(std::coroutine_handle<> h);

    template <typename T>
    TaskHandle<T> spawn(Task<T> t) {
        auto h = std::exchange(t.m_handle, nullptr);

        h.promise().m_runtime = this;
        this->enqueue(h);

        return TaskHandle<T>{h};
    }

    template <typename T>
    T blockOn(Task<T> t) {
        auto h = std::exchange(t.m_handle, nullptr);

        h.promise().m_runtime = this;
        this->enqueue(h);

        TaskHandle<T> handle{h};
        while (!handle.await_ready()) {
            std::this_thread::yield(); // TODO
        }

        if (h.promise().m_exception) {
            std::rethrow_exception(h.promise().m_exception);
        }

        if constexpr (!std::is_void_v<T>) {
            return std::move(h.promise().m_value);
        }
    }

private:
    friend class Runtime;

    std::atomic<bool> m_stopFlag;
    std::mutex m_mtx;
    std::deque<std::coroutine_handle<>> m_tasks;
    std::condition_variable m_cv;
    std::vector<std::thread> m_workers;
    TimeDriver m_timeDriver{this};

    void shutdown();

    void workerLoop(size_t id);
    void timerDriverLoop();
};

template <typename T>
auto spawn(Task<T> t) {
    if (g_runtime) {
        return g_runtime->spawn(std::move(t));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

}