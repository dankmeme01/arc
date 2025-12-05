#pragma once
#include <condition_variable>
#include <mutex>
#include "Waker.hpp"

namespace arc {

/// A wrapper around a condition variable to be used as a waker,
/// used for blocking until a task is complete.
struct CondvarWaker {
public:
    CondvarWaker() = default;
    CondvarWaker(const CondvarWaker&) = delete;
    CondvarWaker& operator=(const CondvarWaker&) = delete;
    CondvarWaker(CondvarWaker&&) = delete;
    CondvarWaker& operator=(CondvarWaker&&) = delete;

    Waker waker() noexcept {
        static constexpr RawWakerVtable vtable = {
            .wake = [](void* data) {
                static_cast<CondvarWaker*>(data)->notify();
            },
            .wakeByRef = [](void* data) {
                static_cast<CondvarWaker*>(data)->notify();
            },
            .clone = [](void* data) -> RawWaker {
                return RawWaker{data, &vtable};
            },
            .destroy = [](void*) {},
        };

        return Waker{this, &vtable};
    }

    void wait() noexcept {
        std::unique_lock lock(mtx);
        while (!notified) {
            cv.wait(lock);
        }
    }

    void notify() noexcept {
        std::unique_lock lock(mtx);
        notified = true;
        cv.notify_one();
    }

    std::unique_lock<std::mutex> lock() noexcept {
        return std::unique_lock{mtx};
    }

private:
    std::condition_variable cv;
    std::mutex mtx;
    bool notified = false;
};

}