# Arc

Arc is a modern C++ async runtime heavily inspired by Tokio and Rust async in general. If you've programmed async rust before - this library will be very familiar to you.

This project is WIP, some things may have bugs and not be production ready, but it is actively maintained and some things are covered by tests. Features (scroll below for examples):
* Runtime that can run using either one or multiple threads
* Tasks as an independent unit of execution
* Blocking tasks on a thread pool
* Synchronization (Mutexes, semaphores, notify, MPSC channels)
* Networking (UDP sockets, TCP sockets and listeners)
* Time utilities (sleep, interval, timeout)
* Multi-future pollers like `arc::select` and `arc::joinAll`
* Signal catching (i.e. listening for Ctrl+C easily)
* Top-level exception handler that prints the backtrace of futures, to aid in debugging

TODO:
* File IO using blocking thread pool
* Better poller. Current implementation uses `poll`/`WSAPoll`, which isn't scalable. This is not an issue for small jobs, but makes the library unsuitable for servers that handle hundreds of connections. Currently this is a non-goal as this library was mostly made for a network client rather than a server

## Getting Started

Arc supports CMake and requires C++23. If you are using CPM, the easiest way to use Arc is as follows:
```cmake
CPMAddPackage("gh:dankmeme01/arc@v1.1.0")
target_link_libraries(mylib PRIVATE arc)
```

To run any async code, you must have a runtime. Arc runtimes do not need to be unique or persistent, there is no global singleton runtime and you are responsible for creating one yourself. If you are a library developer and want to use Arc, you can spin up a runtime and run code like this:
```cpp
#include <arc/prelude.hpp>

arc::Future<int> myFuture() {
    fmt::println("Hello from async!");
    co_return 42;
}

auto rt = arc::Runtime::create(4); // use 4 threads, omit to use the CPU thread count

// this will wait for `myFuture` to finish and return its result
int value = rt->blockOn(myFuture());

// this will spawn the future independently and not block
// note that the future will be aborted if the runtime is destroyed
auto handle = rt->spawn(myFuture());
```

If you are an application developer, you can use a helper macro to automatically make a runtime for you:
```cpp
#include <arc/runtime/Main.hpp>

arc::Future<> asyncMain(int argc, const char** argv) {
    fmt::println("Hello from async!");
    co_return;
}

ARC_DEFINE_MAIN(asyncMain);
// alternatively, if you want to specify thread count
ARC_DEFINE_MAIN_NT(asyncMain, 4);
```

When using the helper macro, the arguments of the main function must be either `()`, `(int, char**)` or `(int, const char**)`. The return value can be:
* `int` (aka `arc::Future<int>`) or any `T` that is convertible to `int` - value will be used as the exit code
* `void` (aka `arc::Future<>` or `arc::Future<void>`) - exit code will be 0
* `geode::Result<void, E>` where `E` can be formatted with `fmt` - returning an `Err` will print the error and exit with code 1

## Examples

Creating a runtime and blocking on a single async function, creating tasks
```cpp
#include <arc/prelude.hpp>
using namespace asp::time;

arc::Future<int> noop() {
    // `yield` temporarily yields control to the scheduler, like a very short sleep
    co_await arc::yield();
    co_return 1;
}

arc::Future<> asyncMain() {
    // `spawn` can be used to spawn a task and let it run in the background,
    // the coroutine will be running in parallel and not block the current task
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
// create an interval that ticks every 250 milliseconds
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
    // Create a new MPSC channel with unlimited capacity
    auto [tx, rx] = arc::mpsc::channel<int>();

    // Spawn a consumer task
    arc::spawn(consumer(std::move(rx)));

    // Sender can be copied, unlike the Receiver
    auto tx2 = tx;

    // Send can only fail if the channel has been closed (meaning the receiver no longer exists)
    (void) co_await tx.send(1);

    // `trySend` can be used in sync or async code, will fail if the channel is closed or full
    auto res = tx2.trySend(2);
}
```

Synchronization utilities such as Mutex, Notify, Semaphore
```cpp
arc::Future<> asyncMain() {
    arc::Mutex<int> mtx{0};
    arc::Notify notify;

    // spawn a task that will wait for a notification
    // if you're confused about `this auto self`, scroll to the bottom of README
    auto handle = arc::spawn([&](this auto self) -> arc::Future<int> {
        co_await notify.notified();

        // try to lock the mutex, this will take some time because main function waits before unlocking
        auto lock = co_await mtx.lock();
        co_return *lock;
    }());

    {
        // lock the mutex and change the value
        fmt::println("Locking mutex in main");
        auto lock = co_await mtx.lock();
        *lock = 42;

        // notify the other task and wait a bit before unlocking
        fmt::println("Notifying task");
        notify.notifyOne();
        co_await arc::sleep(Duration::fromSecs(1));

        fmt::println("Unlocking mutex in main");
    }

    int value = co_await handle;
    fmt::println("{}", value);
}
```

Creating TCP and UDP sockets
```cpp
// TcpStream is very similar to rust's TcpStream
auto res = co_await arc::TcpStream::connect("127.0.0.1:8000");
auto socket = std::move(res).unwrap();

// In the real world, check that the functions actually succeed instead of casting to void/unwrapping
char[] data = "hello world";
(void) co_await socket.send(data, sizeof(data));
char buf[512];
size_t n = (co_await socket.receive(buf, 512)).unwrap();

// UdpSocket
auto res = co_await arc::UdpSocket::bindAny();
auto socket = std::move(res).unwrap();

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
auto listener = std::move(res).unwrap();

while (true) {
    auto res = co_await listener.accept();
    auto [stream, addr] = std::move(res).unwrap();

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

// Custom futures must inherit Pollable and implement the `poll` method
// If your future returns nothing, the `poll` method should return a `bool`, representing whether it's ready or not.
// Otherwise, it should return `optional<T>`, representing the output value if ready.
struct ARC_NODISCARD MyFuture : arc::Pollable<MyFuture, int> {
    int counter = 0;

    std::optional<int> poll(arc::Context& cx) {
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

    // An even simpler way to make a temporary future is to use arc::pollFunc
    int counter = 0;
    auto fut2 = arc::pollFunc([&] -> std::optional<int> {
        return counter++;
    });

    // fut2 can be awaited and has identical functionality as the awaiter above
    int val = co_await fut2;
}

ARC_DEFINE_MAIN(aMain);
```

## Common gotchas

This section is not entirely about Arc, but also about C++ coroutines in general.

### Reference parameteres in futures

Generally, references are always OK if the object they are pointing to is **not temporary**. This means, `T&` is almost always fine, while `const T&` and `T&&` can be dangerous.

Let's take this function that takes a string by a constant reference:
```cpp
Future<size_t> getSize(const std::string& msg) {
    co_return msg.size();
}
```

Here are valid, non-UB ways to invoke it:
```cpp
co_await getSize("string");
co_await arc::spawn(getSize("string"));
co_await arc::timeout(Duration::fromSecs(1), getSize("string"));

std::string s = "string";
auto fut = getSize(s);
co_await fut;
```

Here are problematic ways that will likely lead to a crash:
```cpp
auto fut = getSize("string");
co_await fut;

auto ja = arc::joinAll(getSize("string"), getSize("string 2"));
co_await ja;
```

The examples earlier were ok, because when a temporary `std::string` is made, it exists up until the end of the statement. This includes the `co_await` expression, and even passing the future into other functions, such as `arc::spawn`, `arc::timeout`, `arc::joinAll`, etc. The other two examples store the future in a local variable, which leads to the temporary string being destroyed before the future is awaited.

Rvalue references (`T&&`) suffer from the similar problem. It's usually better to just take a `T` argument and move it into the coroutine frame, rather than taking a `T&&` that might be pointing to an object that is already gone. Every single example listed above will work perfectly fine if you simply change the signature to:
```cpp
Future<size_t> getSize(std::string msg) {
    co_return msg.size();
}
```

The problems above are also very prominent when spawning, for example:
```cpp
auto handle = arc::spawn(getSize("test"));
```

This will be undefined behavior if the string is not accepted by value, as the string ceases to exist after the task is spawned. When writing a function that is likely to be spawned as a task, pay extra attention to how it accepts arguments.

### Lambda futures

Take a look at this seemingly innocent code:

```cpp
int value = 0;
auto fut = [&value] -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
}();
co_await fut;
```

At a first glance it seems fine, although the lambda captures `value` by reference, it can never outlive the variable since it's awaited right away, right? But nope, this is actually a use-after-free :)

This happens because lambda captures only live as long as the lambda itself. By the time we reach this line:
```cpp
}();
```

we complete the lambda invocation, and the lambda is destroyed. And with it, all captures are gone. When the lambda gets actually awaited, and code starts *actually* executing, the captures are dead and should not be used.

This is a mistake that is very easy to make, especially when passing inline futures to another function, for example `arc::timeout`, `arc::select`, etc. Notably, `arc::spawn` is safe from this as long as you pass the *lambda* to it, and not the *future*, because it will store the given lambda until it's no longer needed
```cpp
// Good:
auto task = arc::spawn([&value] -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
});

// Bad, undefined behavior
auto task = arc::spawn([&value] -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
}());
```

There are three easy ways to work around this problem:

1. If possible, don't capture anything. By contrast, this capture-less code will be completely fine, as parameters are stored in the coroutine frame:
```cpp
int value = 0;
auto fut = [](int& value) -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
}(value);
co_await fut;
```

2. Use deducing this to store the lambda object as a parameter in the coroutine frame
```cpp
int value = 0;
auto fut = [&value](this auto self) -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
}();
co_await fut;
```

This syntax might be surprising for those who have never seen C++23 "deducing this" feature, but it's a pretty elegant way to ensure lambda captures live long enough. Make sure you specifically do `this auto self` and not `this const auto& self` or `this auto&& self`, as these are also UB.

3. Store the lambda and make sure it lives as long as necessary. This can be pretty annoying, so one of the methods above should be preferred.
```cpp
int value = 0;
auto lambda = [&value] -> arc::Future<> {
    fmt::println("{}", value);
    co_return;
};
co_await lambda();
```
