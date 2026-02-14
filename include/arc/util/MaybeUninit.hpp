#pragma once
#include <utility>

namespace arc {

template <typename T>
struct MaybeUninit {
    MaybeUninit() noexcept {}
    ~MaybeUninit() noexcept {}

    template <typename... Args>
    void init(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        new (&value) T(std::forward<Args>(args)...);
    }

    void drop() noexcept(noexcept(std::declval<T>().~T())) {
        value.~T();
    }

    T& assumeInit() & noexcept {
        return value;
    }

    const T& assumeInit() const& noexcept {
        return value;
    }

    T assumeInit() && noexcept {
        return std::move(value);
    }

    T* ptr() noexcept {
        return &value;
    }

private:
    union {
        T value;
        char _dummy;
    };
};

}