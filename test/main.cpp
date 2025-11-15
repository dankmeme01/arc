#include <arc/prelude.hpp>
#include <asp/time.hpp>

#include <fmt/format.h>

using namespace asp::time;

arc::Task<int> childTask(int x) {
    fmt::println("running task {}: {}", x, pthread_self());
    co_await arc::sleepFor(Duration::fromSecs(1));
    co_return x + 42;
}

arc::Task<int> noopTask(int y) {
    co_return y;
}

arc::Task<> asyncSleep(Duration dur) {
    fmt::println("Sleeping for {} (thr {})", dur.toString(), pthread_self());
    co_await arc::sleepFor(dur);
}

arc::Task<> syncSleep(Duration dur) {
    fmt::println("Sleeping for {} (thr {})", dur.toString(), pthread_self());
    asp::time::sleep(dur);
    co_return;
}

arc::Task<> testReceiveMpsc(arc::mpsc::Receiver<int> rx) {
    while (true) {
        auto val = co_await rx.recv();

        if (!val) {
            fmt::println("[rx] channel closed, exiting");
            break;
        }

        fmt::println("[rx] received value: {}", *val);
    }

    co_return;
}


arc::Task<> asyncMain() {
    auto [tx, rx] = arc::mpsc::channel<int>();
    auto rxTask = arc::spawn(testReceiveMpsc(std::move(rx)));

    for (int i = 0; i < 5; i++) {
        fmt::println("[tx] sending value: {}", i * 10);
        co_await tx.send(i * 10);
        co_await arc::sleepFor(Duration::fromMillis(500));
    }

    // destroy the sender
    {
        auto tx2 = std::move(tx);
    }

    auto interval = arc::interval(Duration::fromMillis(250));
    while (true) {
        co_await interval;
    }

    co_await rxTask;
    co_return;
}

int main() {
    arc::Runtime runtime{4};
    runtime.blockOn(asyncMain());
    fmt::println("async main completed!");
    return 0;
}