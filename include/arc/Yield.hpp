#pragma once
#include <coroutine>

namespace arc {

struct Yield {
    bool await_ready() noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept;
};

Yield yield() noexcept;

}