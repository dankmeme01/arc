#pragma once
#include <concepts>
#include <coroutine>
#include <optional>
#include <variant>
#include <type_traits>
#include "PollableMetadata.hpp"
#include "Context.hpp"
#include <arc/util/MaybeUninit.hpp>

namespace arc {

// Simple nodiscard helper to mark pollables, since they do nothing unless awaited or polled
#define ARC_NODISCARD [[nodiscard("Pollables do nothing unless polled or awaited")]]

// A pollable is something that can be polled for completion.
// This is similar to the `Future` trait in Rust.

struct PollableVtable {
    using PollFn = bool(*)(void*, Context&);
    using GetOutputFn = void(*)(void*, Context&, void* output);

    const PollableMetadata* m_metadata = nullptr;
    PollFn m_poll = nullptr;
    GetOutputFn m_getOutput = nullptr;

    bool poll(void* self, Context& cx) const {
        return m_poll(self, cx);
    }

    template <typename T>
    T getOutput(void* self, Context& cx) const {
        MaybeUninit<T> output;
        reinterpret_cast<void(*)(void*, Context&, MaybeUninit<T>*)>(m_getOutput)(self, cx, &output);
        return std::move(output.assumeInit());
    }
};

struct PollableBase {
    const PollableVtable* m_vtable = nullptr;
    std::coroutine_handle<> m_parent;

    PollableBase() = default;
    PollableBase(PollableVtable* vtable) : m_vtable(vtable) {}
    PollableBase(const PollableBase&) = delete;
    PollableBase& operator=(const PollableBase&) = delete;
    PollableBase(PollableBase&&) noexcept = default;
    PollableBase& operator=(PollableBase&&) noexcept = default;
    ~PollableBase() = default;

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h);
    void await_resume() noexcept {}

    void attachToParent(std::coroutine_handle<> h) noexcept;
    Context* contextFromParent() const noexcept;
};

template <typename Derived, typename T = void>
struct Pollable : PollableBase {
    using Output = T;

    T await_resume() noexcept(noexcept(m_vtable->getOutput<T>(this, std::declval<Context&>()))) {
        return m_vtable->getOutput<T>(this, *this->contextFromParent());
    }

    inline Pollable() {
        this->m_vtable = &vtable;
    }

protected:
    std::optional<Output> m_output;

    static constexpr PollableVtable vtable = {
        .m_metadata = PollableMetadata::create<Derived>(),

        .m_poll = [](void* self, Context& cx) {
            auto me = reinterpret_cast<Derived*>(self);
            me->Pollable::m_output = me->poll(cx);
            return me->Pollable::m_output.has_value();
        },

        .m_getOutput = [](void* self, Context& cx, void* outp) {
            auto me = reinterpret_cast<Pollable*>(self);
            auto out = reinterpret_cast<MaybeUninit<T>*>(outp);
            out->init(std::move(*me->m_output));
        },
    };
};

template <typename Derived>
struct Pollable<Derived, void> : PollableBase {
    using Output = void;

    inline Pollable() {
        this->m_vtable = &vtable;
    }

protected:
    static constexpr PollableVtable vtable = {
        .m_metadata = PollableMetadata::create<Derived>(),
        .m_poll = [](void* self, Context& cx) {
            return reinterpret_cast<Derived*>(self)->poll(cx);
        },
        .m_getOutput = nullptr,
    };
};

template <typename T>
concept IsPollable = std::derived_from<T, PollableBase>;

/// Utility class that can be used to create an awaiter using a custom poll function on a class
template <typename T, auto F>
struct CustomFnAwaiter : Pollable<CustomFnAwaiter<T, F>> {
    // assume that F is a memfn on T
    explicit CustomFnAwaiter(T* obj) noexcept : m_obj(obj) {}

    bool poll(Context& cx) {
        return (m_obj->*F)(cx);
    }

private:
    T* m_obj;
};

template <typename T>
struct ExtractOptional {
    using type = T;
};

template <typename U>
struct ExtractOptional<std::optional<U>> {
    using type = U;
};

template <typename F> requires std::is_invocable_v<F>
inline auto pollFunc(F&& func) {
    using PollReturn = std::invoke_result_t<F>;
    using Output = std::conditional_t<
        std::is_same_v<PollReturn, bool>,
        void,
        typename ExtractOptional<PollReturn>::type
    >;

    struct PollFuncAwaiter : Pollable<PollFuncAwaiter, Output> {
        explicit PollFuncAwaiter(F&& f) : m_func(std::forward<F>(f)) {}

        PollReturn poll(Context& cx) {
            return m_func();
        }

    private:
        F m_func;
    };

    return PollFuncAwaiter{std::forward<F>(func)};
}

template <typename F> requires std::is_invocable_v<F, Context&>
inline auto pollFunc(F&& func) {
    using PollReturn = std::invoke_result_t<F, Context&>;
    using Output = std::conditional_t<
        std::is_same_v<PollReturn, bool>,
        void,
        typename ExtractOptional<PollReturn>::type
    >;

    struct PollFuncAwaiter : Pollable<PollFuncAwaiter, Output> {
        explicit PollFuncAwaiter(F&& f) : m_func(std::forward<F>(f)) {}

        PollReturn poll(Context& cx) {
            return m_func(cx);
        }

    private:
        F m_func;
    };

    return PollFuncAwaiter{std::forward<F>(func)};
}

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

}