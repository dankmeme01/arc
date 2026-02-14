#pragma once

namespace arc {

struct RawWaker;
struct Waker;

struct RawWakerVtable {
    using WakeFn = void(*)(void*);
    using WakeByRefFn = void(*)(void*);
    using CloneFn = RawWaker(*)(void*);
    using DestroyFn = void(*)(void*);

    WakeFn wake = +[](void*){};
    WakeByRefFn wakeByRef = +[](void*){};
    CloneFn clone = nullptr;
    DestroyFn destroy = +[](void*){};
};

struct RawWaker {
    void* m_data = nullptr;
    const RawWakerVtable* m_vtable = nullptr;

    bool equals(const RawWaker& other) const noexcept;
    bool valid() const noexcept;
    static RawWaker noop() noexcept;
};

struct Waker : RawWaker {
    Waker() noexcept = default;
    Waker(void* data, const RawWakerVtable* vtable) noexcept;
    Waker(RawWaker raw) noexcept;
    Waker(const Waker&) = delete;
    Waker& operator=(const Waker&) = delete;
    Waker(Waker&& other) noexcept;
    Waker& operator=(Waker&& other) noexcept;

    ~Waker();

    operator bool() const noexcept;

    void reset() noexcept;
    void destroy();

    static Waker noop() noexcept;
    void wake() noexcept;
    void wakeByRef() noexcept;
    Waker clone() const noexcept;
};

}