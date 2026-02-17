#include "common.hpp"

Future<> throws() {
    throw std::runtime_error("This is an exception");
    co_return;
}

Future<std::optional<std::string>> nestedThrow(int level = 10) {
    ARC_FRAME();
    if (level == 0) {
        co_await throws();
    }
    co_return co_await nestedThrow(level - 1);
}

Future<> asyncMain() {
    ARC_FRAME();

    arc::setLogFunction([](std::string msg, LogLevel level) {
        fmt::println("{}", msg);
    });

    auto handle = arc::spawn(throws());
    handle.setName("throws task");
    try {
        fmt::println("Pre-await");
        co_await handle;
        fmt::println("Post-await");
    } catch (const std::exception& e) {
        fmt::println("Caught exception: {}", e.what());
    } catch (...) {
        fmt::println("Caught unknown exception");
    }

    arc::spawn(nestedThrow());

    co_await arc::sleep(asp::Duration::fromMillis(1));
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);
