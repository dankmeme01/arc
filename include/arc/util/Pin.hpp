#pragma once
#include <utility>

namespace arc {

/// Pins a value in memory, preventing moves and copies.
template <typename T>
struct Pin {
    T value;

    Pin() : value() {}

    template <typename... Args>
    Pin(Args&&... args) : value(std::forward<Args>(args)...) {}

    ~Pin() = default;

    Pin(const Pin&) = delete;
    Pin& operator=(const Pin&) = delete;
    Pin(Pin&&) = delete;
    Pin& operator=(Pin&&) = delete;

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
