#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include <variant>
#include <optional>

#include <arc/util/Trace.hpp>
#include "Pollable.hpp"

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

template <typename T>
struct Promise;

template <Pollable T>
struct Task;

template <typename T = void>
struct ARC_NODISCARD Future : PollableLowLevelBase<Future<T>, T> {
    using Output = T;
    using NVT = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type m_handle;
    bool m_yielding = false;

    Future(handle_type handle) : m_handle(handle) {
        // Override vtable, to set future to true in metadata
        static const PollableVtable vtable = {
            .poll = [](void* self) {
                return reinterpret_cast<Future*>(self)->poll();
            },

            .getOutput = reinterpret_cast<void*>(+[](void* self) -> T {
                return reinterpret_cast<Future*>(self)->getOutput();
            }),

            .metadata = PollableMetadata::create<Future, true>(),
        };
        this->m_vtable = &vtable;
    }

    Future(Future&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {
        this->m_vtable = other.m_vtable;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            this->destroy();
            this->m_vtable = other.m_vtable;
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

    bool coopYield() {
        if (ctx().shouldCoopYield()) {
            trace("[{}] cooperatively yielding", this->debugName());
            ctx().wake();
            return true;
        }

        return false;
    }

    bool await_ready() noexcept {
        TRACE("[{}] await_ready(), done: {}", this->debugName(), m_handle ? m_handle.done() : true);
        if (this->coopYield()) {
            m_yielding = true;
            return false;
        }

        return m_handle ? m_handle.done() : true;
    }

    bool await_suspend(std::coroutine_handle<> awaiting) {
        TRACE("[{}] await_suspend({}), child: {}", this->debugName(), m_handle.address(), awaiting.address(), (void*)this->child());

        auto awaitingP = std::coroutine_handle<promise_type>::from_address(awaiting.address());
        awaitingP.promise().m_child = this;

        if (m_yielding) {
            m_yielding = false;
            return true;
        }

        bool doSuspend = true;

        // if we don't have a child, wake the current task immediately
        if (!this->child()) {
            TRACE("[{}] await_suspend(): no child, resuming immediately", this->debugName());
            auto& cx = ctx();

            cx.pushFrame(this);
            m_handle.resume();
            cx.maybeRethrow();
            cx.popFrame();

            if (m_handle.done()) {
                doSuspend = false;
            }
        }

        return doSuspend;
    }

    T await_resume() {
        TRACE("[{}] await_resume()", this->debugName());
        return this->getOutput();
    }

    bool poll() {
        auto child = this->child();
        auto resume = [&] {
            m_handle.resume();
            if (m_handle.done()) {
                ctx().maybeRethrow();
                return true;
            }
            return false;
        };

        TRACE("[{}] poll(), child: {}", this->debugName(), (void*)child);

        if (child) {
            bool done = child->vPoll();
            TRACE("[{}] poll() -> child done: {}", this->debugName(), done);
            if (done) {
                return resume();
            }
            return false;
        } else {
            if (m_handle.done()) {
                return true;
            }
            return resume();
        }
    }

    T getOutput() {
        ctx().maybeRethrow();

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
        TRACE("[Promise {}] return_value()", (void*)this);
        static_cast<Derived*>(this)->m_value = std::forward<From>(from);
    }
};

template <typename Derived>
struct PromiseBaseV {
    PollableUniBase* m_child = nullptr;

    void return_void() noexcept {
        TRACE("[Promise {}] return_void()", (void*)this);
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
        TRACE("[Promise {}] unhandled_exception()", (void*)this);

        ctx().onUnhandledException(std::current_exception());
    }

    Future<T> get_return_object() {
        return Future<T>{ Future<T>::handle_type::from_promise(*this) };
    }

    // Final awaiter

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
            auto& p = h.promise();
            // trace(
            //     "[Promise {}] FinalAwaiter::await_suspend({}), child: {}",
            //     (void*)this, h.address(),
            //     (void*)p.m_child
            // );
        }

        void await_resume() noexcept {}
    };

    auto final_suspend() noexcept {
        TRACE("[Promise {}] final_suspend", (void*)this);
        return FinalAwaiter{};
    }

    std::optional<value_type> m_value;
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

template <typename Fut, typename Out = typename FutureTraits<std::decay_t<Fut>>::Output>
inline Future<Out> toPlainFuture(Fut fut) {
    co_return co_await std::move(fut);
}

}