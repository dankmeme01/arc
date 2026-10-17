#include "common.hpp"

static arc::TaskLocalKey<int> g_value;
static arc::TaskLocalKey<std::string> g_str;

static std::atomic<size_t> g_task = 0;

Future<> checkTls() {
    int& val = co_await g_value.awaiter();
    auto& str = co_await g_str.awaiter();
    size_t myTask = g_task++;

    val = rand();
    str = fmt::format("My awesome long string {}", rand());

    fmt::println("({}) Initial values: {} {}", myTask, val, str);
    co_await arc::sleep(asp::Duration::fromMillis(5));
    fmt::println("({}) Values now: {} {}", myTask, val, str);
}

Future<> asyncMain() {
    std::vector<TaskHandle<>> tasks;
    for (size_t i = 0; i < 8; i++) {
        tasks.emplace_back(arc::spawn(checkTls()));
    }
    co_await arc::joinAll(tasks);

    co_return;
}

ARC_DEFINE_MAIN_NT(asyncMain, 8);
