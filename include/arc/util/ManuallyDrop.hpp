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

template <typename T>
void forget(T&& obj) noexcept {
    ManuallyDrop<std::decay_t<T>> md(std::forward<T>(obj));
    // intentionally do not call md.drop()
}

template <typename T>
void drop(T&& obj) {
    std::decay_t<T> temp = std::forward<T>(obj);
}

}