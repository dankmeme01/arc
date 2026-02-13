#pragma once
#include "Pollable.hpp"
#include <arc/util/Trace.hpp>
#include <arc/util/MaybeUninit.hpp>

namespace arc {

struct PromiseVtable {
    using SetDebugNameFn = void(*)(void*, asp::BoxedString);
    using GetDebugNameFn = asp::BoxedString(*)(void*);
    using GetOutputFn = void(*)(void*, void* out);
    using DeliverOutputFn = void(*)(void*, void* value);

    SetDebugNameFn m_setDebugName = nullptr;
    GetDebugNameFn m_getDebugName = nullptr;
    GetOutputFn m_getOutput = nullptr;
    DeliverOutputFn m_deliverOutput = nullptr;

    void setDebugName(void* self, asp::BoxedString name) const {
        m_setDebugName(self, std::move(name));
    }

    asp::BoxedString getDebugName(void* self) const {
        return m_getDebugName(self);
    }

    template <typename T>
    T getOutput(void* self) const {
        MaybeUninit<T> output;
        reinterpret_cast<void (*)(void*, MaybeUninit<T>*)>(m_getOutput)(self, &output);
        return std::move(output.assumeInit());
    }

    template <typename T>
    void deliverOutput(void* self, T* value) const {
        reinterpret_cast<void (*)(void*, T*)>(m_deliverOutput)(self, value);
    }
};

struct PromiseBase {
    void attachChild(PollableBase* child) noexcept {
        m_child = child;
    }

    PollableBase* getChild() noexcept {
        return m_child;
    }

    void setContext(Context* cx) noexcept {
        m_context = cx;
    }

    Context* getContext() noexcept {
        return m_context;
    }

    void setDebugName(asp::BoxedString name) {
        m_vtable->setDebugName(this, std::move(name));
    }

    asp::BoxedString getDebugName() {
        return m_vtable->getDebugName(this);
    }

    template <typename T>
    T getOutput() {
        return m_vtable->getOutput<T>(this);
    }

    template <typename T>
    void deliverOutput(T* value) {
        m_vtable->deliverOutput(this, value);
    }

    void setException(std::exception_ptr exc) {
        m_exception = std::move(exc);
    }

    std::exception_ptr getException() {
        return m_exception;
    }

    struct CurrentAwaiter {
        PromiseBase* promise = nullptr;

        bool await_ready() noexcept { return false; }

        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
            promise = &h.promise();
            return h;
        }
        PromiseBase* await_resume() noexcept { return promise; }
    };

    static auto current() noexcept {
        return CurrentAwaiter{};
    }

protected:
    const PromiseVtable* m_vtable;
    PollableBase* m_child = nullptr;
    Context* m_context = nullptr;
    std::exception_ptr m_exception;

    // Every field past this comment can be changed without causing an ABI break.
    // Fields must never be directly accessed and should only be used through the vtable.
    // Fields above must stay stable, they are accessed by offset for performance reasons.
    asp::BoxedString m_debugName;

    static void vSetDebugName(void* self, asp::BoxedString name) {
        reinterpret_cast<PromiseBase*>(self)->m_debugName = std::move(name);
    }
    static asp::BoxedString vGetDebugName(void* self) {
        auto& name = reinterpret_cast<PromiseBase*>(self)->m_debugName;
        return name;
    }
};

struct PromiseBaseV : PromiseBase {
    PromiseBaseV() {
        m_vtable = &vtable;
    }

    void return_void() noexcept {
        // ARC_TRACE("[Promise {}] return_void()", (void*)this);
    }

protected:
    static constexpr PromiseVtable vtable = {
        .m_setDebugName = &PromiseBase::vSetDebugName,
        .m_getDebugName = &PromiseBase::vGetDebugName,
        .m_getOutput = nullptr,
        .m_deliverOutput = nullptr,
    };
};

template <typename R>
struct PromiseBaseNV : PromiseBase {
    PromiseBaseNV() {
        m_vtable = &vtable;
    }

    template <std::convertible_to<R> From>
    void return_value(From&& from) {
        // ARC_TRACE("[Promise {}] return_value()", (void*)this);
        R value = static_cast<R>(std::forward<From>(from));
        this->deliverOutput(&value);
    }

protected:
    std::optional<R> m_value;

    static constexpr PromiseVtable vtable = {
        .m_setDebugName = &PromiseBase::vSetDebugName,
        .m_getDebugName = &PromiseBase::vGetDebugName,
        .m_getOutput = [](void* self, void* outp) {
            auto me = reinterpret_cast<PromiseBaseNV*>(self);
            auto out = reinterpret_cast<MaybeUninit<R>*>(outp);
            out->init(std::move(*me->m_value));
        },
        .m_deliverOutput = [](void* self, void* valuep) {
            auto me = reinterpret_cast<PromiseBaseNV*>(self);
            auto value = reinterpret_cast<R*>(valuep);
            me->m_value = std::move(*value);
        },
    };
};

template <typename T>
struct Promise : std::conditional_t<
    std::is_void_v<T>,
    PromiseBaseV,
    PromiseBaseNV<T>
> {
    using return_type = T;
    using promise_type = Promise<T>;
    using value_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    Promise() = default;

    std::suspend_always initial_suspend() noexcept { return {}; }

    void unhandled_exception() {
        ARC_TRACE("[Promise {}] unhandled_exception()", (void*)this);

        auto exc = std::current_exception();
        this->getContext()->onUnhandledException();
        this->setException(exc);
    }

    // Defined in Future.hpp
    auto get_return_object() noexcept;

    // Final awaiter

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}
        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {}
    };

    auto final_suspend() noexcept {
        // ARC_TRACE("[Promise {}] final_suspend()", (void*)this);
        return FinalAwaiter{};
    }
};


}

#undef TRACE