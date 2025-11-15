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


// arc::Task<> asyncMain() {
//     auto [tx, rx] = arc::mpsc::channel<int>();
//     auto rxTask = arc::spawn(testReceiveMpsc(std::move(rx)));

//     for (int i = 0; i < 5; i++) {
//         fmt::println("[tx] sending value: {}", i * 10);
//         co_await tx.send(i * 10);
//         co_await arc::sleepFor(Duration::fromMillis(500));
//     }

//     // destroy the sender
//     {
//         auto tx2 = std::move(tx);
//     }

//     auto interval = arc::interval(Duration::fromMillis(250));
//     while (true) {
//         co_await interval;
//     }

//     co_await rxTask;
//     co_return;
// }

arc::Task<> lockerFn(size_t i, arc::Mutex<int>& mtx, Duration wait) {
    fmt::println("Thread {}, waiting..", i);
    auto guard = co_await mtx.lock();
    fmt::println("Thread {}, holding lock for {}", i, wait.toString());
    co_await arc::sleepFor(wait);
    fmt::println("Thread {}, releasing lock", i);
}

arc::Task<> lockTests() {
    arc::Mutex<int> mtx{0};

    auto t1 = arc::spawn(lockerFn(1, mtx, Duration::fromSecs(2)));
    auto t2 = arc::spawn(lockerFn(2, mtx, Duration::fromSecs(1)));
    auto t3 = arc::spawn(lockerFn(3, mtx, Duration::fromSecs(3)));
    auto t4 = arc::spawn(lockerFn(4, mtx, Duration::fromSecs(1)));

    co_await arc::joinAll(
        std::move(t1),
        std::move(t2),
        std::move(t3),
        std::move(t4)
    );

    // co_await t1;
    // co_await t2;
    // co_await t3;
    // co_await t4;
}

int main() {
    arc::Runtime runtime{4};
    runtime.blockOn(lockTests());
    fmt::println("async main completed!");
    return 0;
}