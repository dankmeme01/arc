#include <arc/Yield.hpp>
#include <arc/Runtime.hpp>

namespace arc {

bool Yield::await_ready() noexcept {
    return false;
}

void Yield::await_suspend(std::coroutine_handle<> h) noexcept {
    g_runtime->enqueue(h);
}

void Yield::await_resume() noexcept {}

Yield yield() noexcept {
    return Yield{};
}

}