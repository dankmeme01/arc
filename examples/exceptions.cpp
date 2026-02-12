#include "common.hpp"

Future<> throws() {
    ARC_FRAME();
    throw std::runtime_error("This is an exception");
    co_return;
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
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);
