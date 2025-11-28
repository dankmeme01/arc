#pragma once
#include <utility>

namespace arc {

template <typename T>
struct ManuallyDrop {
    union {
        T value;
        char _dummy;
    };

    ManuallyDrop() : value() {}

    template <typename... Args>
    ManuallyDrop(Args&&... args) : value(std::forward<Args>(args)...) {}

    ~ManuallyDrop() noexcept {}

    void drop() noexcept(noexcept(std::declval<T>().~T())) {
        value.~T();
    }

    T* operator->() noexcept {
        return &value;
    }

    T& operator*() noexcept {
        return value;
    }

    T& get() noexcept {
        return value;
    }
};

}