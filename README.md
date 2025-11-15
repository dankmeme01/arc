# Arc

Modern C++ async runtime inspired by Tokio.

This project is heavily WIP, few things are implemented and they likely have bugs.

## Examples

Creating a runtime and blocking on a single async function, creating tasks
```cpp
#include <arc/prelude.hpp>
using namespace asp::time;

arc::Task<int> noop() {
    co_await arc::yield();
    co_return 1;
}

arc::Task<> asyncMain() {
    // This task will run in the background and not block the current task
    auto handle = arc::spawn(noop());

    // Async sleep, does not block the thread and yields to the runtime instead.
    co_await arc::sleepFor(Duration::fromSecs(1));

    // One second later, let's retrieve the value returned by the spawned task
    int value = co_await handle;
    std::cout << value << std::endl;

    co_return;
}

int main() {
    arc::Runtime runtime;
    runtime.blockOn(asyncMain());
}
```

Running a task every X seconds (interval)
```cpp
arc::Task<> asyncMain() {
    auto interval = arc::interval(Duration::fromMillis(250));
    while (true) {
        co_await interval;
        std::cout << "tick!" << std::endl;
    }
}
```

Sending data between tasks or between sync code
```cpp
arc::Task<> consumer(arc::mpsc::Receiver<int> rx) {
    while (true) {
        auto val = co_await rx.recv();

        if (!val) {
            break; // channel is closed now, all senders have been destroyed
        }

        std::cout << "received value: " << *val << std::endl;
    }
}

arc::Task<> asyncMain() {
    // The channel can be created in sync code as well as async
    auto [tx, rx] = arc::mpsc::channel<int>();

    arc::spawn(consumer(std::move(rx)));

    // Sender can be copied, unlike the Receiver
    auto tx2 = tx;

    co_await tx.send(1);

    // `trySend` can be used in sync code
    tx2.trySend(2);
}
```

Using async locks and joining multiple tasks
```cpp
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
}
```

