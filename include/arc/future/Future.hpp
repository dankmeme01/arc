#pragma once
#include <coroutine>
#include <utility>
#include <variant>

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

template <IsPollable Fut, typename Lambda>
struct Task;

template <typename T = void>
struct ARC_NODISCARD Future : PollableBase {
    using Output = T;
    using NonVoidOutput = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Future(handle_type handle) noexcept requires (std::is_void_v<T>) : m_handle(handle) {
        m_vtable = &vtableVoid;
    }

    Future(handle_type handle) noexcept requires (!std::is_void_v<T>) : m_handle(handle) {
        m_vtable = &vtableNonVoid;
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

    promise_type& promise() noexcept {
        return m_handle.promise();
    }

    PollableBase* child() noexcept {
        return this->promise().getChild();
    }

    std::string debugName() {
        auto defaultName = [&] { return fmt::format("Future @ {}", (void*)m_handle.address()); };

        auto dname = this->promise().getDebugName();
        return dname.empty() ? defaultName() : std::string(dname);
    }

    void setDebugName(asp::BoxedString name) {
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
            cx->popFrame();

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

    bool poll(Context& cx) noexcept {
        ARC_DEBUG_ASSERT(m_handle, "polling a future with an invalid handle");

        auto child = this->child();
        auto resume = [&] noexcept {
            auto& promise = this->promise();
            promise.setContext(&cx);

            cx.pushFrame(this);

            try {
                m_handle.resume();
            } catch (const std::exception& e) {
                printError("[{}] future threw when calling handle.resume(): {}", this->debugName(), e.what());
                cx.dumpStack();
                std::terminate();
            }

            cx.popFrame();

            if (m_handle.done()) {
                return true;
            }
            return false;
        };

        TRACE("[{}] poll(), child: {}", this->debugName(), (void*)child);

        if (child) {
            cx.pushFrame(this);

            bool done = child->m_vtable->poll(child, cx);
            cx.popFrame();

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

    T getOutput() {
        this->maybeRethrow();
        if constexpr (!std::is_void_v<T>) {
            return this->promise().template getOutput<T>();
        }
    }

protected:
    handle_type m_handle{};

    static constexpr PollableVtable vtableVoid = {
        .m_poll = [](void* self, Context& cx) noexcept {
            return reinterpret_cast<Future*>(self)->poll(cx);
        },
        .m_getOutput = [](void* self, void* outp) {
            reinterpret_cast<Future*>(self)->getOutput();
        },
        .m_metadata = PollableMetadata::create<Future, true>(),
    };

    static constexpr PollableVtable vtableNonVoid = {
        .m_poll = [](void* self, Context& cx) noexcept {
            return reinterpret_cast<Future*>(self)->poll(cx);
        },
        .m_getOutput = [](void* self, void* outp) {
            auto out = reinterpret_cast<MaybeUninit<NonVoidOutput>*>(outp);
            out->init(reinterpret_cast<Future*>(self)->getOutput());
        },
        .m_metadata = PollableMetadata::create<Future, true>(),
    };

    void maybeRethrow() {
        auto exc = this->promise().getException();
        trace("[{}] maybeRethrow(), exception: {}", this->debugName(), exc ? "yes" : "no");
        if (exc) {
            std::rethrow_exception(exc);
        }
    }
};

template <typename T>
auto Promise<T>::get_return_object() noexcept {
    return Future<T>{ Future<T>::handle_type::from_promise(*this) };
}

template <typename Fut, typename Out = typename FutureTraits<std::decay_t<Fut>>::Output>
inline Future<Out> toPlainFuture(Fut fut) {
    co_return co_await std::move(fut);
}

}

#undef TRACE


// Main promise : 0x7c80603e0050 , select promise : 0x7c70603ec050