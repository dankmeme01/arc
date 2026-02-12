#pragma once
#include "Pollable.hpp"
#include <arc/util/Trace.hpp>
#include <arc/util/MaybeUninit.hpp>

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

struct PromiseVtable {
    using AttachChildFn = void(*)(void*, PollableBase*);
    using GetChildFn = PollableBase*(*)(void*);
    using SetContextFn = void(*)(void*, Context*);
    using GetContextFn = Context*(*)(void*);
    using SetDebugNameFn = void(*)(void*, std::string);
    using GetDebugNameFn = std::string_view(*)(void*);
    using GetOutputFn = void(*)(void*, void* out);
    using DeliverOutputFn = void(*)(void*, void* value);
    using SetExceptionFn = void(*)(void*, std::exception_ptr);
    using GetExceptionFn = std::exception_ptr(*)(void*);

    AttachChildFn m_attachChild = nullptr;
    GetChildFn m_getChild = nullptr;
    SetContextFn m_setContext = nullptr;
    GetContextFn m_getContext = nullptr;
    SetDebugNameFn m_setDebugName = nullptr;
    GetDebugNameFn m_getDebugName = nullptr;
    GetOutputFn m_getOutput = nullptr;
    DeliverOutputFn m_deliverOutput = nullptr;
    SetExceptionFn m_setException = nullptr;
    GetExceptionFn m_getException = nullptr;

    void attachChild(void* self, PollableBase* child) const noexcept {
        m_attachChild(self, child);
    }

    PollableBase* getChild(void* self) const noexcept {
        return m_getChild(self);
    }

    void setContext(void* self, Context* cx) const noexcept {
        TRACE("[Promise {}] setContext({})", self, (void*)cx);
        m_setContext(self, cx);
    }

    Context* getContext(void* self) const noexcept {
        auto ctx = m_getContext(self);
        TRACE("[Promise {}] getContext() -> {}", self, (void*)ctx);
        return ctx;
    }

    void setDebugName(void* self, std::string name) const {
        m_setDebugName(self, std::move(name));
    }

    std::string_view getDebugName(void* self) const {
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

    void setException(void* self, std::exception_ptr exc) const {
        m_setException(self, exc);
    }

    std::exception_ptr getException(void* self) const {
        return m_getException(self);
    }
};

struct PromiseBase {
    void attachChild(PollableBase* child) noexcept {
        m_vtable->attachChild(this, child);
    }

    PollableBase* getChild() noexcept {
        return m_vtable->getChild(this);
    }

    void setContext(Context* cx) noexcept {
        m_vtable->setContext(this, cx);
    }

    Context* getContext() noexcept {
        return m_vtable->getContext(this);
    }

    void setDebugName(std::string name) {
        m_vtable->setDebugName(this, std::move(name));
    }

    std::string_view getDebugName() {
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
        trace("SetException({}, {})", (void*)this, exc ? "yes" : "no");
        m_vtable->setException(this, exc);
    }

    std::exception_ptr getException() {
        trace("GetException({})", (void*)this);
        return m_vtable->getException(this);
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
    // Every field past the vtable can be changed without causing an ABI break.
    // Fields must never be directly accessed and should only be used through the vtable.
    const PromiseVtable* m_vtable;
    PollableBase* m_child = nullptr;
    Context* m_context = nullptr;
    std::string m_debugName;
    std::exception_ptr m_exception;

    static void vAttachChild(void* self, PollableBase* child) noexcept {
        reinterpret_cast<PromiseBase*>(self)->m_child = child;
    }
    static PollableBase* vGetChild(void* self) noexcept {
        return reinterpret_cast<PromiseBase*>(self)->m_child;
    }
    static void vSetContext(void* self, Context* cx) noexcept {
        reinterpret_cast<PromiseBase*>(self)->m_context = cx;
    }
    static Context* vGetContext(void* self) noexcept {
        return reinterpret_cast<PromiseBase*>(self)->m_context;
    }
    static void vSetDebugName(void* self, std::string name) {
        reinterpret_cast<PromiseBase*>(self)->m_debugName = std::move(name);
    }
    static std::string_view vGetDebugName(void* self) {
        auto& name = reinterpret_cast<PromiseBase*>(self)->m_debugName;
        return name;
    }
    static void vSetException(void* self, std::exception_ptr exc) {
        reinterpret_cast<PromiseBase*>(self)->m_exception = exc;
    }
    static std::exception_ptr vGetException(void* self) {
        return reinterpret_cast<PromiseBase*>(self)->m_exception;
    }
};

struct PromiseBaseV : PromiseBase {
    PromiseBaseV() {
        m_vtable = &vtable;
    }

    void return_void() noexcept {
        TRACE("[Promise {}] return_void()", (void*)this);
    }

protected:
    static constexpr PromiseVtable vtable = {
        .m_attachChild = &PromiseBase::vAttachChild,
        .m_getChild = &PromiseBase::vGetChild,
        .m_setContext = &PromiseBase::vSetContext,
        .m_getContext = &PromiseBase::vGetContext,
        .m_setDebugName = &PromiseBase::vSetDebugName,
        .m_getDebugName = &PromiseBase::vGetDebugName,
        .m_getOutput = nullptr,
        .m_deliverOutput = nullptr,
        .m_setException = &PromiseBase::vSetException,
        .m_getException = &PromiseBase::vGetException,
    };
};

template <typename R>
struct PromiseBaseNV : PromiseBase {
    PromiseBaseNV() {
        m_vtable = &vtable;
    }

    template <std::convertible_to<R> From>
    void return_value(From&& from) {
        TRACE("[Promise {}] return_value()", (void*)this);
        R value = static_cast<R>(std::forward<From>(from));
        this->deliverOutput(&value);
    }

protected:
    std::optional<R> m_value;

    static constexpr PromiseVtable vtable = {
        .m_attachChild = &PromiseBase::vAttachChild,
        .m_getChild = &PromiseBase::vGetChild,
        .m_setContext = &PromiseBase::vSetContext,
        .m_getContext = &PromiseBase::vGetContext,
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
        .m_setException = &PromiseBase::vSetException,
        .m_getException = &PromiseBase::vGetException,
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
        trace("[Promise {}] unhandled_exception()", (void*)this);

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
        TRACE("[Promise {}] final_suspend()", (void*)this);
        return FinalAwaiter{};
    }
};


}

#undef TRACE