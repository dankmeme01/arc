#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include <variant>
#include <optional>

#include <arc/Util.hpp>
#include "Pollable.hpp"

namespace arc {

template <typename T>
struct Promise;

template <Pollable T>
struct Task;

template <typename T = void>
struct Future : PollableBase<Future<T>, T> {
    using Output = T;
    using NVT = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type m_handle;

    Future(handle_type handle) : m_handle(handle) {}

    Future(Future&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            this->destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }

    ~Future() {
        this->destroy();
    }

    void setDebugName(std::string_view name) {
        this->promise().m_debugName = name.substr(0, 16);
    }

    std::string_view debugName() {
        if (this->promise().m_debugName.empty()) {
            this->promise().m_debugName = fmt::format("Future @ {}", (void*)m_handle.address());
        }

        return this->promise().m_debugName;
    }

    void destroy() {
        if (m_handle) {
            m_handle.destroy();
            m_handle = {};
        }
    }

    promise_type& promise() {
        return m_handle.promise();
    }

    PollableUniBase* child() {
        return this->promise().m_child;
    }

    bool await_ready() noexcept {
        trace("[{}] await_ready(), done: {}", this->debugName(), m_handle ? m_handle.done() : true);
        return m_handle ? m_handle.done() : true;
    }

    bool await_suspend(std::coroutine_handle<> awaiting) {
        trace("[{}] await_suspend({}), child: {}", this->debugName(), m_handle.address(), awaiting.address(), (void*)this->child());

        auto awaitingP = std::coroutine_handle<promise_type>::from_address(awaiting.address());
        awaitingP.promise().m_child = this;

        bool doSuspend = true;

        // if we don't have a child, wake the current task immediately
        if (!this->child()) {
            trace("[{}] await_suspend(): no child, resuming immediately", this->debugName());
            m_handle.resume();

            if (m_handle.done()) {
                doSuspend = false;
            }
        }

        return doSuspend;
    }

    T await_resume() {
        trace("[{}] await_resume()", this->debugName());
        return this->getOutput();
    }

    bool poll() {
        auto child = this->child();
        trace("[{}] poll(), child: {}", this->debugName(), (void*)child);

        if (child) {
            bool done = child->vPoll();
            trace("[{}] poll() -> child done: {}", this->debugName(), done);
            if (done) {
                m_handle.resume();
                return m_handle.done();
            }
            return false;
        } else {
            if (!m_handle.done()) {
                m_handle.resume();
            }
            return m_handle.done();
        }
    }

    T getOutput() {
        if (this->promise().m_exception) {
            std::rethrow_exception(this->promise().m_exception);
        }

        if constexpr (!std::is_void_v<T>) {
            return std::move(*this->promise().m_value);
        }
    }
};

template <typename R, typename Derived>
struct PromiseBaseNV {
    PollableUniBase* m_child = nullptr;
    template <std::convertible_to<R> From>
    void return_value(From&& from) {
        trace("[Promise {}] return_value()", (void*)this);
        static_cast<Derived*>(this)->m_value = std::forward<From>(from);
    }
};

template <typename Derived>
struct PromiseBaseV {
    PollableUniBase* m_child = nullptr;

    void return_void() noexcept {
        trace("[Promise {}] return_void()", (void*)this);
    }
};

template <typename T>
using PromiseBase = std::conditional_t<
    std::is_void_v<T>,
    PromiseBaseV<Promise<T>>,
    PromiseBaseNV<T, Promise<T>>
>;

template <typename T>
struct Promise : PromiseBase<T> {
    using return_type = T;
    using promise_type = Promise<T>;
    using value_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    Promise() = default;

    std::suspend_always initial_suspend() noexcept { return {}; }

    void unhandled_exception() {
        m_exception = std::current_exception();
    }

    Future<T> get_return_object() {
        return Future<T>{ Future<T>::handle_type::from_promise(*this) };
    }

    // Final awaiter

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
            auto& p = h.promise();
            trace(
                "[Promise {}] FinalAwaiter::await_suspend({}), child: {}",
                (void*)this, h.address(),
                (void*)p.m_child
            );
        }

        void await_resume() noexcept {}
    };

    auto final_suspend() noexcept {
        trace("[Promise {}] final_suspend", (void*)this);
        return FinalAwaiter{};
    }

    std::optional<value_type> m_value;
    std::exception_ptr m_exception;
    std::string m_debugName;
};

// Convenience struct for getting stuff like output from an awaitable

template <typename T>
concept Awaitable = requires(T t) {
    { t.await_resume() };
};

template <Awaitable Fut>
struct FutureTraits {
    using Output = decltype(std::declval<Fut>().await_resume());
    using NonVoidOutput = std::conditional_t<std::is_void_v<Output>, std::monostate, Output>;
};

template <typename T>
struct IsFuture : std::false_type {};
template <Pollable T>
struct IsFuture<T> : std::true_type {};

}