#include "common.hpp"

struct ThrowingPollable : arc::Pollable<ThrowingPollable> {
    bool poll(arc::Context& cx) {
        throw std::runtime_error("This pollable always throws");
        return true;
    }
};

struct NothrowPollable : arc::NoexceptPollable<NothrowPollable, void> {
    bool poll(arc::Context& cx) noexcept {
        return true;
    }
};

Future<> asyncMain() {
    co_await NothrowPollable{};

    try {
        co_await ThrowingPollable{};
        fmt::println("Caught no exception");
    } catch (const std::exception& e) {
        fmt::println("Caught exception from ThrowingPollable: {}", e.what());
    } catch (...) {
        fmt::println("Caught unknown exception from ThrowingPollable");
    }
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);
