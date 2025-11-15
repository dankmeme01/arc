#pragma once
#include <coroutine>
#include <concepts>
#include <utility>
#include <variant>
#include <exception>

namespace arc {

struct Runtime;
inline thread_local Runtime* g_runtime = nullptr;

template <typename T>
struct TaskPromise;
template <typename T>
struct TaskHandle;

template <typename T = void>
struct Task {
    using promise_type = TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type m_handle;

    Task(handle_type handle) : m_handle(handle) {}
    Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (m_handle) m_handle.destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }

    ~Task() {
        if (m_handle) m_handle.destroy();
    }

    bool await_ready() noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> awaiting) {
        auto& p = m_handle.promise();
        p.m_continuation = awaiting;
        p.m_runtime = arc::g_runtime;
        p.m_runtime->enqueue(m_handle);
    }

    T await_resume() {
        if (m_handle.promise().m_exception) {
            std::rethrow_exception(m_handle.promise().m_exception);
        }

        if constexpr (!std::is_void_v<T>) {
            return std::move(m_handle.promise().m_value);
        }
    }
};

template <typename R, typename Derived>
struct TaskPromiseBaseNV {
    template <std::convertible_to<R> From>
    void return_value(From&& from) {
        static_cast<Derived*>(this)->m_value = std::forward<From>(from);
    }
};

template <typename Derived>
struct TaskPromiseBaseV {
    void return_void() noexcept {}
};

template <typename T>
using TaskPromiseBase = std::conditional_t<
    std::is_void_v<T>,
    TaskPromiseBaseV<TaskPromise<T>>,
    TaskPromiseBaseNV<T, TaskPromise<T>>
>;

template <typename T>
struct TaskPromise : TaskPromiseBase<T> {
    friend struct TaskPromiseBaseV<TaskPromise<T>>;
    friend struct TaskPromiseBaseNV<T, TaskPromise<T>>;
    friend struct Task<T>;
    friend struct TaskHandle<T>;
    friend struct Runtime;

    using return_type = T;
    using promise_type = TaskPromise<T>;
    using value_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    TaskPromise() = default;

    std::suspend_always initial_suspend() noexcept { return {}; }

    void unhandled_exception() {
        m_exception = std::current_exception();
    }

    Task<T> get_return_object() noexcept {
        return Task<T>{ Task<T>::handle_type::from_promise(*this) };
    }

    // Final awaiter

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
            auto& p = h.promise();

            if (p.m_continuation) {
                p.m_runtime->enqueue(p.m_continuation);
            }
        }

        void await_resume() noexcept {}
    };

    auto final_suspend() noexcept {
        return FinalAwaiter{};
    }

private:
    value_type m_value;
    std::exception_ptr m_exception;
    std::coroutine_handle<> m_continuation;
    Runtime* m_runtime = nullptr;
};

template <typename T>
struct TaskHandle {
    using handle_type = std::coroutine_handle<TaskPromise<T>>;
    handle_type m_handle;

    bool await_ready() const noexcept {
        return m_handle.done();
    }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        auto& p = m_handle.promise();
        p.m_runtime = g_runtime;
        p.m_continuation = awaiting;
    }

    T await_resume() {
        if (m_handle.promise().m_exception) {
            std::rethrow_exception(m_handle.promise().m_exception);
        }

        if constexpr (!std::is_void_v<T>) {
            return std::move(m_handle.promise().m_value);
        }
    }
};

}