#pragma once
#include <concepts>
#include <coroutine>

namespace arc {

// A pollable is something that can be polled for completion. This is similar to the `Future` trait in Rust.

struct PollableVtable {
    using PollFn = bool(*)(void*);

    PollFn poll = nullptr;
    void* getOutput = nullptr;
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

    bool poll() {
        return m_vtable->poll(this);
    }

    template <typename T>
    T getOutput() {
        return reinterpret_cast<T (*)(void*)>(m_vtable->getOutput)(this);
    }

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}
};

template <typename Derived, typename T>
struct PollableBase : PollableUniBase {
    using Output = T;

    T await_resume() noexcept(noexcept(getOutput())) {
        return this->getOutput();
    }

    T getOutput() {
        return PollableUniBase::getOutput<T>();
    }

    inline PollableBase() {
        static const PollableVtable vtable = {
            .poll = [](void* self) {
                return reinterpret_cast<Derived*>(self)->pollImpl();
            },
            .getOutput = reinterpret_cast<void*>(+[](void* self) -> T {
                return reinterpret_cast<Derived*>(self)->getOutputImpl();
            }),
        };

        this->m_vtable = &vtable;
    }
};

template <typename Derived>
struct PollableBase<Derived, void> : PollableUniBase {
    using Output = void;
    void getOutput() {}

    static constexpr PollableVtable vtable = {
        .poll = [](void* self) {
            return reinterpret_cast<Derived*>(self)->pollImpl();
        },
        .getOutput = nullptr,
    };

    inline PollableBase() {
        this->m_vtable = &vtable;
    }
};

template <typename T>
concept Pollable = requires(T t) {
    { t.poll() } -> std::same_as<bool>;
    typename T::Output;
    { t.getOutput() } -> std::same_as<typename T::Output>;
};

}