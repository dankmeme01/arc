#pragma once
#include "Pollable.hpp"
#include "Future.hpp"

#if 1
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

    AttachChildFn m_attachChild = nullptr;
    GetChildFn m_getChild = nullptr;
    SetContextFn m_setContext = nullptr;
    GetContextFn m_getContext = nullptr;
    SetDebugNameFn m_setDebugName = nullptr;
    GetDebugNameFn m_getDebugName = nullptr;
    void* m_getOutput = nullptr;
    void* m_deliverOutput = nullptr;

    void attachChild(void* self, PollableBase* child) const {
        m_attachChild(self, child);
    }

    PollableBase* getChild(void* self) const {
        return m_getChild(self);
    }

    void setContext(void* self, Context* cx) const {
        TRACE("[Promise {}] setContext({})", self, (void*)cx);
        m_setContext(self, cx);
    }

    Context* getContext(void* self) const {
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
        return reinterpret_cast<T (*)(void*)>(m_getOutput)(self);
    }

    template <typename T>
    void deliverOutput(void* self, T* value) const {
        reinterpret_cast<void (*)(void*, T*)>(m_deliverOutput)(self, value);
    }
};

struct PromiseBase {
    // Every field past the vtable can be changed without causing an ABI break.
    // Fields must never be directly accessed and should only be used through the vtable.
    const PromiseVtable* m_vtable;
    PollableBase* m_child = nullptr;
    Context* m_context = nullptr;
    std::string m_debugName;

    void attachChild(PollableBase* child) {
        m_vtable->attachChild(this, child);
    }

    PollableBase* getChild() {
        return m_vtable->getChild(this);
    }

    void setContext(Context* cx) {
        m_vtable->setContext(this, cx);
    }

    Context* getContext() {
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

    static auto current() {
        return CurrentAwaiter{};
    }

protected:
    static void vAttachChild(void* self, PollableBase* child) {
        reinterpret_cast<PromiseBase*>(self)->m_child = child;
    }
    static PollableBase* vGetChild(void* self) {
        return reinterpret_cast<PromiseBase*>(self)->m_child;
    }
    static void vSetContext(void* self, Context* cx) {
        reinterpret_cast<PromiseBase*>(self)->m_context = cx;
    }
    static Context* vGetContext(void* self) {
        return reinterpret_cast<PromiseBase*>(self)->m_context;
    }
    static void vSetDebugName(void* self, std::string name) {
        reinterpret_cast<PromiseBase*>(self)->m_debugName = std::move(name);
    }
    static std::string_view vGetDebugName(void* self) {
        auto& name = reinterpret_cast<PromiseBase*>(self)->m_debugName;
        return name;
    }
};

struct PromiseBaseV : PromiseBase {
    PromiseBaseV() {
        static const PromiseVtable vtable = {
            .m_attachChild = &PromiseBase::vAttachChild,
            .m_getChild = &PromiseBase::vGetChild,
            .m_setContext = &PromiseBase::vSetContext,
            .m_getContext = &PromiseBase::vGetContext,
            .m_setDebugName = &PromiseBase::vSetDebugName,
            .m_getDebugName = &PromiseBase::vGetDebugName,
            .m_getOutput = nullptr,
            .m_deliverOutput = nullptr,
        };

        m_vtable = &vtable;
    }

    void return_void() noexcept {
        trace("[Promise {}] return_void()", (void*)this);
    }
};

template <typename R>
struct PromiseBaseNV : PromiseBase {
    PromiseBaseNV() {
        static const PromiseVtable vtable = {
            .m_attachChild = &PromiseBase::vAttachChild,
            .m_getChild = &PromiseBase::vGetChild,
            .m_setContext = &PromiseBase::vSetContext,
            .m_getContext = &PromiseBase::vGetContext,
            .m_setDebugName = &PromiseBase::vSetDebugName,
            .m_getDebugName = &PromiseBase::vGetDebugName,
            .m_getOutput = reinterpret_cast<void*>(+[](void* self) -> R {
                auto me = reinterpret_cast<PromiseBaseNV*>(self);
                return std::move(*me->m_value);
            }),
            .m_deliverOutput = reinterpret_cast<void*>(+[](void* self, R* value) {
                auto me = reinterpret_cast<PromiseBaseNV*>(self);
                me->m_value = std::move(*value);
            }),
        };

        m_vtable = &vtable;
    }

    template <std::convertible_to<R> From>
    void return_value(From&& from) {
        trace("[Promise {}] return_value()", (void*)this);
        R value = static_cast<R>(std::forward<From>(from));
        this->deliverOutput(&value);
    }

protected:
    std::optional<R> m_value;
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

        this->getContext()->onUnhandledException(std::current_exception());
    }

    Future<T> get_return_object() {
        return Future<T>{ Future<T>::handle_type::from_promise(*this) };
    }

    // Final awaiter

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}
        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {}
    };

    auto final_suspend() noexcept {
        trace("[Promise {}] final_suspend()", (void*)this);
        return FinalAwaiter{};
    }
};


}

#undef TRACE