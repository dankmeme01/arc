#pragma once
#include <utility>

namespace arc {

/// This type is NOT safe for types whose move constructor is non-trivial.
template <typename T>
struct ManuallyDrop {
    ManuallyDrop() requires std::is_default_constructible_v<T> : value() {}

    template <typename... Args>
    ManuallyDrop(Args&&... args) : value(std::forward<Args>(args)...) {}

    ~ManuallyDrop() noexcept {}

    ManuallyDrop(const ManuallyDrop&) = delete;
    ManuallyDrop& operator=(const ManuallyDrop&) = delete;

    ManuallyDrop(ManuallyDrop&&) = delete;
    ManuallyDrop& operator=(ManuallyDrop&&) = delete;

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

private:
    union {
        T value;
        char _dummy;
    };
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