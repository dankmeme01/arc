#include <arc/task/Yield.hpp>
#include <arc/task/Context.hpp>
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

bool CoopYield::poll() {
    if (yielded) return true;

    if (!ctx().shouldCoopYield()) {
        return true;
    }

    yielded = true;
    ctx().wake();

    return false;
}

CoopYield coopYield() noexcept {
    return CoopYield{};
}

}