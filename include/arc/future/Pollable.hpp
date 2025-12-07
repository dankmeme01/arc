#pragma once
#include <concepts>
#include <coroutine>
#include <optional>
#include "PollableMetadata.hpp"
#include <arc/task/Context.hpp>

namespace arc {

// A pollable is something that can be polled for completion. This is similar to the `Future` trait in Rust.

struct PollableVtable {
    using PollFn = bool(*)(void*);

    PollFn poll = nullptr;
    void* getOutput = nullptr;
    const PollableMetadata* metadata = nullptr;
};

struct PollableUniBase {
    const PollableVtable* m_vtable;

    PollableUniBase() = default;
    PollableUniBase(PollableVtable* vtable) : m_vtable(vtable) {}
    PollableUniBase(const PollableUniBase&) = delete;
    PollableUniBase& operator=(const PollableUniBase&) = delete;
    PollableUniBase(PollableUniBase&&) = default;
    PollableUniBase& operator=(PollableUniBase&&) = default;
    ~PollableUniBase() = default;

    bool vPoll() {
        ctx().pushFrame(this);
        bool result = m_vtable->poll(this);
        ctx().popFrame();
        return result;
    }

    template <typename T>
    T vGetOutput() {
        return reinterpret_cast<T (*)(void*)>(m_vtable->getOutput)(this);
    }

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h);
    void await_resume() noexcept {}
};

template <typename Derived, typename T = void>
struct PollableLowLevelBase : PollableUniBase {
    using Output = T;

    T await_resume() noexcept(noexcept(vGetOutput<T>())) {
        return this->vGetOutput<T>();
    }

    inline PollableLowLevelBase() {
        static const PollableVtable vtable = {
            .poll = [](void* self) {
                return reinterpret_cast<Derived*>(self)->poll();
            },

            .getOutput = reinterpret_cast<void*>(+[](void* self) -> T {
                return reinterpret_cast<Derived*>(self)->getOutput();
            }),

            .metadata = PollableMetadata::create<Derived>(),
        };

        this->m_vtable = &vtable;
    }
};

template <typename Derived>
struct PollableLowLevelBase<Derived, void> : PollableUniBase {
    using Output = void;
    void getOutput() {}

    inline PollableLowLevelBase() {
        static const PollableVtable vtable = {
            .poll = [](void* self) {
                return reinterpret_cast<Derived*>(self)->poll();
            },
            .getOutput = nullptr,
            .metadata = PollableMetadata::create<Derived>(),
        };

        this->m_vtable = &vtable;
    }
};

template <typename Derived, typename T = void>
struct PollableBase : PollableUniBase {
    using Output = T;

    T await_resume() noexcept(noexcept(vGetOutput<T>())) {
        return this->template vGetOutput<T>();
    }

    inline PollableBase() {
        static const PollableVtable vtable = {
            .poll = [](void* self) {
                auto me = reinterpret_cast<Derived*>(self);
                me->_pb_output() = me->poll();
                return me->_pb_output().has_value();
            },

            .getOutput = reinterpret_cast<void*>(+[](void* self) -> T {
                auto me = reinterpret_cast<Derived*>(self);
                auto out = std::move(*me->_pb_output());
                me->_pb_output().reset();
                return out;
            }),

            .metadata = PollableMetadata::create<Derived>(),
        };

        this->m_vtable = &vtable;
    }

    std::optional<Output>& _pb_output() {
        return m_output;
    }

protected:
    std::optional<Output> m_output;
};

template <typename Derived>
struct PollableBase<Derived, void> : PollableLowLevelBase<Derived, void> {};

template <typename T>
concept Pollable = std::derived_from<T, PollableUniBase>;

/// Utility class that can be used to create an awaiter using a custom poll function on a class
template <typename T, auto F>
struct CustomFnAwaiter : PollableBase<CustomFnAwaiter<T, F>> {
    // assume that F is a memfn on T
    explicit CustomFnAwaiter(T* obj) : m_obj(obj) {}

    bool poll() {
        return (m_obj->*F)();
    }

private:
    T* m_obj;
};


/// Like CustomFnAwaiter, but the poll method returns std::optional<T> instead,
/// representing the output value if ready.
template <typename T, auto F, typename Output = std::invoke_result_t<decltype(F)>>
struct CustomFnAwaiterNV : PollableBase<CustomFnAwaiterNV<T, F>, Output> {
    explicit CustomFnAwaiterNV(T* obj) : m_obj(obj) {}

    bool poll() {
        m_output = (m_obj->*F)();
        return m_output.has_value();
    }

    Output getOutput() {
        auto out = std::move(*m_output);
        m_output.reset();
        return out;
    }

private:
    T* m_obj;
    std::optional<Output> m_output;
};

template <typename T>
struct ExtractOptional {
    using type = T;
};

template <typename U>
struct ExtractOptional<std::optional<U>> {
    using type = U;
};

template <typename F> requires (!std::is_reference_v<F> && std::is_invocable_v<F>)
inline auto pollFunc(F&& func) {
    using PollReturn = std::invoke_result_t<F>;
    using Output = std::conditional_t<
        std::is_same_v<PollReturn, bool>,
        void,
        typename ExtractOptional<PollReturn>::type
    >;

    struct PollFuncAwaiter : PollableBase<PollFuncAwaiter, Output> {
        explicit PollFuncAwaiter(F&& f) : m_func(std::move(f)) {}

        PollReturn poll() {
            return m_func();
        }

    private:
        F m_func;
    };

    return PollFuncAwaiter{std::forward<F>(func)};
}

}