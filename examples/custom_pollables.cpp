#include "common.hpp"

Future<> asyncMain() {
    int x = co_await arc::pollFunc([] -> std::optional<int> {
        return 42;
    });

    printWarn("{}", x);
    x = co_await arc::pollFunc([](arc::Context& cx) -> std::optional<int> {
        return 42;
    });
    printWarn("{}", x);
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);