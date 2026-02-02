#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include <variant>
#include <optional>

#include <arc/util/Trace.hpp>
#include <arc/util/Assert.hpp>
#include "Context.hpp"
#include "Pollable.hpp"
#include "Promise.hpp"

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

template <typename T>
struct Promise;

template <IsPollable T>
struct Task;

template <typename T = void>
struct ARC_NODISCARD Future : PollableBase {
    using Output = T;
    using NonVoidOutput = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Future(handle_type handle) : m_handle(handle) {
        static const PollableVtable vtable = {
            .m_metadata = PollableMetadata::create<Future, true>(),
            .m_poll = [](void* self, Context& cx) {
                return reinterpret_cast<Future*>(self)->poll(cx);
            },
            .m_getOutput = reinterpret_cast<void*>(+[](void* self, Context& cx) -> T {
                return reinterpret_cast<Future*>(self)->getOutput(cx);
            }),
        };
        m_vtable = &vtable;
    }

    Future(Future&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {
        m_vtable = other.m_vtable;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            this->destroy();
            m_vtable = other.m_vtable;
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }

    ~Future() {
        this->destroy();
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

    PollableBase* child() {
        return this->promise().getChild();
    }

    std::string debugName() {
        auto defaultName = [&] { return fmt::format("Future @ {}", (void*)m_handle.address()); };

        auto dname = this->promise().getDebugName();
        return dname.empty() ? defaultName() : std::string(dname);
    }

    void setDebugName(std::string name) {
        this->promise().setDebugName(std::move(name));
    }

    bool coopYield(Context& cx) {
        if (!cx.shouldCoopYield()) return false;

        trace("[{}] cooperatively yielding", this->debugName());
        cx.wake();
        return true;
    }

    bool await_ready() noexcept {
        TRACE("[{}] await_ready(), done: {}", this->debugName(), m_handle ? m_handle.done() : true);
        return m_handle ? m_handle.done() : true;
    }

    bool await_suspend(std::coroutine_handle<> awaiting) {
        TRACE("[{}] await_suspend({}), child: {}", this->debugName(), awaiting.address(), (void*)this->child());

        this->attachToParent(awaiting);

        auto cx = this->contextFromParent();
        ARC_ASSERT(cx, "context is null in await_suspend");

        if (this->coopYield(*cx)) {
            return true;
        }

        this->promise().setContext(cx);

        bool doSuspend = true;

        // TODO: maybe remove these in favor of poll doing everything?

        // if we don't have a child, wake the current task immediately
        if (!this->child()) {
            TRACE("[{}] await_suspend(): no child, resuming immediately", this->debugName());

            cx->pushFrame(this);
            m_handle.resume();
            cx->maybeRethrow();
            cx->popFrame();

            if (m_handle.done()) {
                doSuspend = false;
            }
        }

        return doSuspend;
    }

    T await_resume() {
        TRACE("[{}] await_resume()", this->debugName());

        auto cx = this->promise().getContext();
        TRACE("[{}] await_resume(): context from promise: {}", this->debugName(), (void*)cx);

        ARC_DEBUG_ASSERT(cx, "context is null in await_resume");

        return this->getOutput(*cx);
    }

    bool poll(Context& cx) {
        ARC_DEBUG_ASSERT(m_handle, "polling a future with an invalid handle");

        auto child = this->child();
        auto resume = [&] {
            auto& promise = this->promise();
            promise.setContext(&cx);
            m_handle.resume();

            if (m_handle.done()) {
                cx.maybeRethrow();
                return true;
            }
            return false;
        };

        TRACE("[{}] poll(), child: {}", this->debugName(), (void*)child);

        if (child) {
            bool done = child->m_vtable->poll(child, cx);
            TRACE("[{}] poll() -> child done: {}", this->debugName(), done);
            if (done) {
                this->promise().attachChild(nullptr);
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


    T getOutput(Context& cx) {
        cx.maybeRethrow();

        if constexpr (!std::is_void_v<T>) {
            return this->promise().template getOutput<T>();
        }
    }

    handle_type m_handle{};
};

template <typename T>
auto Promise<T>::get_return_object() {
    return Future<T>{ Future<T>::handle_type::from_promise(*this) };
}

template <typename Fut, typename Out = typename FutureTraits<std::decay_t<Fut>>::Output>
inline Future<Out> toPlainFuture(Fut fut) {
    co_return co_await std::move(fut);
}

}

#undef TRACE


// Main promise : 0x7c80603e0050 , select promise : 0x7c70603ec050