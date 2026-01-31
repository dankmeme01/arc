#include <arc/task/Yield.hpp>
#include <arc/runtime/Runtime.hpp>

namespace arc {

bool Yield::poll(Context& cx) {
    if (!yielded) {
        yielded = true;
        cx.wake();
        return false;
    } else {
        return true;
    }
}

Yield yield() noexcept {
    return Yield{};
}

bool Never::poll(Context& cx) {
    return false;
}

Never never() noexcept {
    return Never{};
}

bool CoopYield::poll(Context& cx) {
    if (yielded) return true;

    if (!cx.shouldCoopYield()) {
        return true;
    }

    yielded = true;
    cx.wake();

    return false;
}

CoopYield coopYield() noexcept {
    return CoopYield{};
}

}