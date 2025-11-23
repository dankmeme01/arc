# Arc

Arc is a modern C++ async runtime heavily inspired by Tokio and Rust async in general. If you've programmed async rust before - this library will be very familiar to you.

Tasks are the primary unit of execution. The very first function that is ran (your async main function) will be spawned as a task and `blockOn` will return once it's finished. Tasks are intended to be lightweight, and you can always spawn a new task with `arc::spawn(fut)`.

This project is heavily WIP, few things are implemented and they likely have bugs. TODO:

* Foreign future support? awaiting things that aren't `arc::Future<>` or don't inherit `PollableUniBase` at all.
* MPSC channels
* Async IO (backed by blocking thread pool for files, by a poller driver for networking)

## Usage

```cmake
CPMAddPackage("gh:dankmeme01/arc#main")
```

```cpp
#include <arc/prelude.hpp>

arc::Future<> aMain() {
    fmt::println("Hello from async!");
}

ARC_DEFINE_MAIN(aMain);
// Alternatively, you can define your own `main` function for runtime management:
int main() {
    arc::Runtime rt;
    rt.blockOn(aMain());
}
```

## Examples

Creating a runtime and blocking on a single async function, creating tasks
```cpp
#include <arc/prelude.hpp>
using namespace asp::time;

arc::Future<int> noop() {
    co_await arc::yield();
    co_return 1;
}

arc::Future<> asyncMain() {
    // This task will run in the background and not block the current task
    auto handle = arc::spawn(noop());

    // Async sleep, does not block the thread and yields to the runtime instead.
    co_await arc::sleepFor(Duration::fromSecs(1));

    // One second later, let's retrieve the value returned by the spawned task
    int value = co_await handle;
    fmt::println("{}", value);
}

ARC_DEFINE_MAIN(asyncMain);
```

Running a task every X seconds (interval)
```cpp
arc::Future<> asyncMain() {
    auto interval = arc::interval(Duration::fromMillis(250));
    while (true) {
        co_await interval;
        fmt::println("tick!");
    }
}
```

Sending data between tasks or between sync code (TODO: unimpl right now)
```cpp
arc::Task<> consumer(arc::mpsc::Receiver<int> rx) {
    while (true) {
        auto val = co_await rx.recv();

        if (!val) {
            break; // channel is closed now, all senders have been destroyed
        }

        fmt::println("received value: {}", *val);
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

Synchronization utilities such as Mutex, Notify, Semaphore
```cpp
arc::Future<> lockerFn(arc::Mutex<int>& mtx, arc::Notify& notify) {
    co_await arc::yield();

    // 2. wait for the main task to release the lock
    auto guard = co_await mtx.lock();
    fmt::println("Task acquired lock, value: {}", *guard);
    co_await arc::sleepFor(Duration::fromMillis(500));

    // 3. send notification to main task
    notify.notifyOne();
}

arc::Future<> lockTests() {
    arc::Mutex<int> mtx{0};
    arc::Notify notify;

    arc::spawn(lockerFn(mtx, notify));

    {
        // 1. lock the mutex, change the value and wait a bit before unlocking
        auto lock = co_await mtx.lock();
        *lock = 42;
        co_await arc::sleep(Duration::fromSecs(1));
        fmt::println("Unlocking mutex in main");
    }

    // 4. wait until worker notifies us
    co_await notify.notified();
}
```

Run multiple futures concurrently (as part of one task), wait for one of them to complete and cancel the losers. This is very similar to the `tokio::select!` macro in Rust and can be incredibly useful.

```cpp
arc::Future<> aMain() {
    arc::Mutex<int> mtx;

    // arc::select takes an unlimited list of selectees.
    // Whenever the first one of them completes, its callback is invoked (if any),
    // and the rest are immediately cancelled.
    co_await arc::select(
        // A future that simply finishes in 5 seconds
        // (basically ensuring the select won't last longer than that)
        arc::selectee(
            arc::sleep(Duration::fromSecs(5)),
            [] { fmt::println("Time elapsed!"); }
        ),

        // A future that never completes, just for showcase purposes
        arc::selectee(arc::never()),

        // A future that will complete once we are able to
        // acquire the lock on the mutex
        arc::selectee(
            mtx.lock(),
            [](auto guard) { fmt::println("Value: {}", *guard); },
            // Passing `false` as the 3rd argument to `selectee` will
            // disable this branch from being polled.
            false
        ),

        // A future that waits for an interrupt (Ctrl+C) signal to be sent
        arc::selectee(
            arc::ctrl_c(),
            // Callbacks can be synchronous, but they also can be futures
            [] -> arc::Future<> {
                fmt::println("Ctrl+C received, exiting!");
                co_return;
            }
        )
    );
}
```

Creating custom futures
```cpp
#include <arc/future/Pollable.hpp>

// Custom futures must inherit PollableBase and implement the `poll` method
// If your future returns a value, the `getOutput` method
// also must exist and return the output value.
struct MyFuture : arc::PollableBase<MyFuture, int> {
    int counter = 0;

    bool poll() {
        // Let's make a simple generator, that yields numbers forever
        // It's always ready, so always return true
        counter++;
        return true;
    }

    // This is where you return the result.
    // This function is guaranteed to be called after `poll` returns true.
    int getOutput() {
        return counter;
    }
};

arc::Future<> aMain() {
    auto fut = MyFuture{};
    while (true) {
        int x = co_await fut;
        fmt::println("{}", x);
        co_await arc::sleep(Duration::fromMillis(500));
    }
}

ARC_DEFINE_MAIN(aMain);
```