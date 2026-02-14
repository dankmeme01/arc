#pragma once
#include <concepts>
#include <coroutine>
#include <optional>
#include <variant>
#include <type_traits>
#include "PollableMetadata.hpp"
#include "Context.hpp"
#include <arc/util/MaybeUninit.hpp>
#include <arc/util/Config.hpp>
#include <asp/detail/config.hpp>

namespace arc {

// Simple nodiscard helper to mark pollables, since they do nothing unless awaited or polled
#define ARC_NODISCARD [[nodiscard("Pollables do nothing unless polled or awaited")]]

// A pollable is something that can be polled for completion.
// This is similar to the `Future` trait in Rust.

struct PollableVtable {
    using PollFn = bool(*)(void*, Context&) noexcept;
    using GetOutputFn = void(*)(void*, void* output);

    PollFn m_poll = nullptr;
    GetOutputFn m_getOutput = nullptr;
    const PollableMetadata* m_metadata = nullptr;

    bool poll(void* self, Context& cx) const noexcept {
        return m_poll(self, cx);
    }

    template <typename T>
    T getOutput(void* self) const {
        MaybeUninit<T> output;
        reinterpret_cast<void(*)(void*, MaybeUninit<T>*)>(m_getOutput)(self, &output);
        return std::move(output.assumeInit());
    }
};

template <>
inline void PollableVtable::getOutput<void>(void* self) const {
    if (m_getOutput) m_getOutput(self, nullptr);
}

template <typename T>
constexpr bool IsNothrowPollable = noexcept(std::declval<T>().poll(std::declval<Context&>()));

struct PollableBase {
    const PollableVtable* m_vtable = nullptr;

    PollableBase() = default;
    PollableBase(PollableVtable* vtable) : m_vtable(vtable) {}
    PollableBase(const PollableBase&) = delete;
    PollableBase& operator=(const PollableBase&) = delete;
    PollableBase(PollableBase&&) noexcept = default;
    PollableBase& operator=(PollableBase&&) noexcept = default;
    ~PollableBase() = default;

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h);
    void await_resume() {
        m_vtable->getOutput<void>(this);
    }

protected:
    std::coroutine_handle<> m_parent;

    /// Fast path for await_suspend, does not go through the vtable if the pollable is known at compile time
    template <typename Derived>
    bool fastSuspend(this Derived& self, std::coroutine_handle<> h) {
        constexpr bool IsNothrowPollable = ::arc::IsNothrowPollable<Derived>;

        auto cx = contextFromHandle(h);
        bool ready = self.template vPoll<IsNothrowPollable>(&self, *cx);
        if (ready) {
            // fast path - no need to attach to parent
            return false;
        }

        // attach to parent and suspend
        self.attachToParent(h);
        return true;
    }

    void attachToParent(std::coroutine_handle<> h) noexcept;
    Context* contextFromParent() const noexcept;
    static Context* contextFromHandle(std::coroutine_handle<> h) noexcept;
};

template <typename Derived, typename T = void, bool NothrowPoll = false>
struct Pollable : PollableBase {
    using Output = T;

    bool await_suspend(this auto& self, std::coroutine_handle<> h) noexcept(NothrowPoll) {
        return self.template fastSuspend<Derived>(h);
    }

    T await_resume() noexcept(noexcept(m_vtable->getOutput<T>(this))) {
        return m_vtable->getOutput<T>(this);
    }

    inline Pollable() {
        this->m_vtable = &vtable;
    }

protected:
    friend class PollableBase;

    std::optional<Output> m_output;
    ARC_NO_UNIQUE_ADDRESS std::conditional_t<NothrowPoll, std::monostate, std::exception_ptr> m_exception;

    template <bool Nothrow>
    static bool vPoll(void* self, Context& cx) noexcept {
        auto me = static_cast<Pollable*>(self);

        if constexpr (Nothrow) {
            me->m_output = static_cast<Derived*>(me)->poll(cx);
            return me->m_output.has_value();
        } else {
            try {
                me->m_output = static_cast<Derived*>(me)->poll(cx);
            } catch (...) {
                me->m_exception = std::current_exception();
            }
            return me->m_output.has_value() || !!me->m_exception;
        }
    }

    template <bool Nothrow>
    static void vGetOutput(void* self, void* outp) {
        auto me = static_cast<Pollable*>(self);

        if constexpr (!Nothrow) {
            if (me->m_exception) {
                std::rethrow_exception(me->m_exception);
            }
        }

        auto out = reinterpret_cast<MaybeUninit<T>*>(outp);
        out->init(std::move(*me->m_output));
    }

    inline static constexpr PollableVtable vtable = [] {
        constexpr auto meta = PollableMetadata::create<Derived>();

        // instead of just checking the template argument, check if the actual poll function is noexcept
        // here we can actually do it, unlike in the class scope
#if defined __clang__ || defined __GNUC__
        constexpr bool IsNothrowPollable = NothrowPoll && ::arc::IsNothrowPollable<Derived>;
#else
        constexpr bool IsNothrowPollable = NothrowPoll;
#endif

        static_assert(!NothrowPoll || IsNothrowPollable, "if NoexceptPollable is used, the poll function MUST be noexcept");

        return PollableVtable {
            .m_poll = &vPoll<IsNothrowPollable>,
            .m_getOutput = &vGetOutput<IsNothrowPollable>,
            .m_metadata = meta,
        };
    }();
};

template <typename Derived, bool NothrowPoll>
struct Pollable<Derived, void, NothrowPoll> : PollableBase {
    using Output = void;

    bool await_suspend(this auto& self, std::coroutine_handle<> h) noexcept(NothrowPoll) {
        return self.template fastSuspend<Derived>(h);
    }

    inline Pollable() {
        this->m_vtable = &vtable;
    }

protected:
    friend class PollableBase;

    ARC_NO_UNIQUE_ADDRESS std::conditional_t<NothrowPoll, std::monostate, std::exception_ptr> m_exception;

    template <bool Nothrow>
    static bool vPoll(void* self, Context& cx) noexcept {
        auto me = static_cast<Pollable*>(self);

        if constexpr (Nothrow) {
            return static_cast<Derived*>(self)->poll(cx);
        } else {
            try {
                return static_cast<Derived*>(me)->poll(cx);
            } catch (...) {
                me->m_exception = std::current_exception();
                return true;
            }
        }
    }

    template <bool Nothrow>
    static void vGetOutput(void* self, void* outp) {
        auto me = static_cast<Pollable*>(self);
        if (me->m_exception) {
            std::rethrow_exception(me->m_exception);
        }
    }

    inline static constexpr PollableVtable vtable = [] {
        constexpr auto meta = PollableMetadata::create<Derived>();

        // instead of checking the template argument, check if the actual poll function is noexcept
        // here we can actually do it, unlike in the class scope
#if defined __clang__ || defined __GNUC__
        constexpr bool IsNothrowPollable = NothrowPoll && ::arc::IsNothrowPollable<Derived>;
#else
        constexpr bool IsNothrowPollable = NothrowPoll;
#endif
        static_assert(!NothrowPoll || IsNothrowPollable, "if NoexceptPollable is used, the poll function MUST be noexcept");

        if constexpr (IsNothrowPollable) {
            return PollableVtable {
                .m_poll = &vPoll<true>,
                .m_getOutput = nullptr,
                .m_metadata = meta,
            };
        } else {
            return PollableVtable {
                .m_poll = &vPoll<false>,
                .m_getOutput = &vGetOutput<false>,
                .m_metadata = meta,
            };
        }
    }();
};

template <typename Derived, typename T = void>
using NoexceptPollable = Pollable<Derived, T, true>;

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
    constexpr bool Nothrow = std::is_nothrow_invocable_v<F>;

    struct PollFuncAwaiter : Pollable<PollFuncAwaiter, Output, Nothrow> {
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
    constexpr bool Nothrow = std::is_nothrow_invocable_v<F, Context&>;

    struct PollFuncAwaiter : Pollable<PollFuncAwaiter, Output, Nothrow> {
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