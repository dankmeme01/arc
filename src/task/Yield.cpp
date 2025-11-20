#include <arc/task/Yield.hpp>
#include <arc/runtime/Runtime.hpp>

namespace arc {

bool Yield::poll() {
    if (!yielded) {
        yielded = true;
        ctx().wake();
        return false;
    } else {
        return true;
    }
}

Yield yield() noexcept {
    return Yield{};
}

bool Never::poll() {
    return false;
}

Never never() noexcept {
    return Never{};
}

}