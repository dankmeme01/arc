# Arc

Arc is a modern C++ async runtime heavily inspired by Tokio and Rust async in general. If you've programmed async rust before - this library will be very familiar to you.

Tasks are the primary unit of execution. The very first function that is ran (your async main function) will be spawned as a task and `blockOn` will return once it's finished. Tasks are intended to be lightweight, and you can always spawn a new task with `arc::spawn(fut)`.

This project is heavily WIP, few things are implemented and they likely have bugs. TODO:

* File IO using blocking thread pool

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

When using the macro, the main function can either accept no arguments or accept `(int argc, char** argv)` (argv must **NOT** be `const char**`). It must return either `Future<>` (aka `Future<void>`) or `Future<T>` where `T` is convertible to `int`.

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
auto interval = arc::interval(Duration::fromMillis(250));
while (true) {
    co_await interval.tick();
    fmt::println("tick!");
}
```

Sending data between tasks or between sync code
```cpp
arc::Task<> consumer(arc::mpsc::Receiver<int> rx) {
    while (true) {
        auto val = co_await rx.recv();

        if (!val.isOk()) {
            break; // channel is closed now, all senders have been destroyed
        }

        fmt::println("received value: {}", val.unwrap());
    }
}

arc::Task<> asyncMain() {
    // The channel can be created in sync code as well as async
    auto [tx, rx] = arc::mpsc::channel<int>();

    arc::spawn(consumer(std::move(rx)));

    // Sender can be copied, unlike the Receiver
    auto tx2 = tx;

    co_await tx.send(1);

    // `trySend` can be used in sync or async code
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

Creating TCP and UDP sockets
```cpp
// TcpStream is very similar to rust's TcpStream
auto res = co_await arc::TcpStream::connect("127.0.0.1:8000");
auto socket = res.unwrap();

// In the real world, check that the functions actually succeed instead of casting to void/unwrapping
char[] data = "hello world";
(void) co_await socket.send(data, sizeof(data));
char buf[512];
size_t n = (co_await socket.receive(buf, 512)).unwrap();

// UdpSocket
auto res = co_await arc::UdpSocket::bindAny();
auto socket = res.unwrap();

auto dest = qsox::SocketAddress::parse("127.0.0.1:1234").unwrap();
char[] data = "hello world";
(void) co_await socket.sendTo(data, sizeof(data), dest);
char buf[512];
size_t n = (co_await socket.receive(buf, 512)).unwrap();
```

Creating a TCP listener
```cpp
auto res = co_await arc::TcpListener::bind(
    qsox::SocketAddress::parse("0.0.0.0:4242").unwrap()
);
auto listener = res.unwrap();

while (true) {
    auto res = co_await listener.accept();
    auto [stream, addr] = res.unwrap();

    fmt::println("Accepted connection from {}", addr.toString());

    arc::spawn([](arc::TcpStream s, qsox::SocketAddress a) mutable -> arc::Future<> {
        // do things with the socket ...
    }(std::move(stream), addr));
}
```

Putting a time limit on a future, and cancelling it if it doesn't complete in time.
```cpp
auto [tx, rx] = arc::mpsc::channel<int>();

// Wait until we either get a value, or don't get any values in 5 seconds.
auto res = co_await arc::timeout(
    Duration::fromSecs(5),
    rx.recv()
);

if (res.isErr()) {
    fmt::println("Timed out!");
    co_return;
}

auto result = std::move(res).unwrap();
if (result.isOk()) {
    fmt::println("Value: {}", result.unwrap());
} else {
    fmt::println("Channel closed!");
}
```

Run multiple futures concurrently (as part of one task), wait for one of them to complete and cancel the losers. This is very similar to the `tokio::select!` macro in Rust and can be incredibly useful.

```cpp
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
```

Creating custom futures
```cpp
#include <arc/future/Pollable.hpp>

// Custom futures must inherit PollableBase and implement the `poll` method
// If your future returns nothing, the `poll` method should return a `bool`, representing whether it's ready or not.
// Otherwise, it should return `optional<T>`, representing the output value if ready.
struct ARC_NODISCARD MyFuture : arc::PollableBase<MyFuture, int> {
    int counter = 0;

    std::optional<int> poll() {
        // Let's make a simple generator, that yields numbers forever.
        // It is always ready, so just return the number
        return counter++;
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