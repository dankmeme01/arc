# Pollables

The most basic way to create async code are coroutines (functions returning `Future<T>`), they are fairly easy to write and in most async codebases, 95% of the async code you write would consist of just coroutines.

However, sometimes they might not be flexible enough. Coroutines always incur heap allocation for every call (unless optimized out by the compiler), and don't give you full control over the resulting state machine. Almost all Arc primitives do **not** use coroutines in their implementations - for example `arc::yield()`, `arc::sleep()`, etc. all are based on the concept called **Pollable** (also known as **awaitables**). Under the hood, every `Future<T>` is also a pollable, where every poll attempts to advance the coroutine state forward.

Unlike coroutines, plain pollables are cheap - they use a small amount of space on the stack and never allocate, at the cost of being slightly more difficult to create. There are 3 ways to make your own pollable and we will explain them in order from simplest to more advanced, so that it's not immediately overwhelming.

## Poll function

At the heart of every pollable is the poll function. This function has one job - try to make progress on the pollable. If it succeeded and the pollable has finished running, it returns the result. Otherwise, it simply tells the caller that it's still pending. The function `arc::pollFunc` allows you to create an anonymous pollable using just the poll function, which is useful when the function is small and/or may need to capture the environment:

```cpp
#include <arc/future/Pollable.hpp>

int value = co_await arc::pollFunc([] -> std::optional<int> {
    return 42;
});
```

Here, we provide a poll function that returns an `std::optional<int>`, which in this case always returns the value `42` immediately, and thus will always be instantly ready. If we instead did this:

```cpp
int value = co_await arc::pollFunc([] -> std::optional<int> {
    return std::nullopt;
});
```

The pollable would be polled once, claim that it's pending (by returning `std::nullopt`) and then never polled again. Obviously you want it to complete eventually, so let's say you want it to return after a certain global flag gets set. Initially, you may try this:

```cpp
std::atomic<bool> g_flag{false};

int value = co_await arc::pollFunc([] -> std::optional<int> {
    if (g_flag.load()) {
        return 42;
    }
    return std::nullopt;
});
```

But you will notice that even after setting the flag (from another thread), the pollable is still stuck, and never completes. This is completely by design, and it's because it won't be polled again until its task is **woken up**. When you return `std::nullopt` from the `poll` method, you promise to the caller (and eventually to the runtime) that you have scheduled a wakeup, and one will happen as soon as your pollable is ready to be polled again. To schedule a wakeup, you need to obtain the waker from the current task context:

```cpp
std::atomic<bool> g_flag{false};
std::optional<arc::Waker> g_waker;

// pollFunc lambda may also take a context argument for getting the waker
int value = co_await arc::pollFunc([](arc::Context& cx) -> std::optional<int> {
    if (g_flag.load()) {
        return 42;
    }

    // register our waker
    g_waker = cx.cloneWaker();
    return std::nullopt;
});

// This can be called by another thread to wake our task
void wake() {
    g_flag.store(true);
    if (g_waker) {
        g_waker->wake();
        g_waker.reset();
    }
}
```

All futures and pollables must run in the context of a `arc::Task`, and an `arc::Waker` is a simple handle to the task where it's running. Once a pollable that is pending is polled, the entire task is *suspended*, and will never be woken up "automatically" in any way by the runtime. The only way to wake the task and poll it again is to use the waker. This is the core principle of **cooperative multithreading**.

Ignoring the fact that it's not thread safe, the example above is roughly how most synchronization primitives are built in Arc. For example `arc::Mutex` clones the waker and inserts it into the waitlist if the task failed to acquire the lock, and then the task that holds the lock will wake the first waiter when unlocking. Here's an example of how you could reimplement `arc::yield()` yourself:

```cpp
// Note: if your pollable has no output value,
// std::optional can be replaced with a simple bool representing readiness.
co_await arc::pollFunc([yielded = false](arc::Context& cx) mutable -> bool {
    if (!yielded) {
        yielded = true;
        cx.wake();
        return false; // pending, suspend
    }
    return true; // ready
});
```

The first poll (which happens immediately when `co_await`ing) will set `yielded` to `true`, and schedule the task to be woken up again. Because the future is pending, this causes the task to initially suspend, but the `wake()` call (shorthand for `waker()->wake()`) causes it to be put at the back of the executor's queue, and thus likely immediately unsuspended after. This leads to a second poll, which would simply return `true` and complete the `co_await` expression.

## Pollable struct

When writing custom pollables, you often need to store certain state (they are state machines after all :p). For example, the yield example above needs to store a single bool, which means you have to shove it into the lambda captures and make the lambda mutable, which will definitely get annoying if you have more state. To create a custom pollable struct, simply inherit `Pollable` and provide the output type:

```cpp
// The ARC_NODISCARD here is optional, but it's great to catch mistakes
struct ARC_NODISCARD Yield : arc::Pollable<Yield, void> {
    bool poll(arc::Context& cx) {
        if (!yielded) {
            yielded = true;
            cx.wake();
            return false;
        }
        return true;
    }

private:
    bool yielded = false;
};

// Now you can simply await the Yield struct:
co_await Yield{};
```

A couple of quick notes:
* `arc::Pollable` accepts two template arguments - the concrete pollable type (this is [CRTP](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern)) and the actual output type (if the poll function returns `std::optional<int>`, you want simply `int` there)
* `void` can be omitted for void pollables, `arc::Pollable<Yield>` is valid

The main advantage of pollable structs over `pollFunc` is that they're reusable and easier to manage (e.g. you can add helper methods or fields). Under the hood, `pollFunc` simply creates an anonymous pollable struct for you, so there's little difference between them.

If your pollable cannot throw, you should always mark the `poll` method as `noexcept` and inherit `arc::NoexceptPollable<Derived, T>` instead - this lets Arc eliminate all exception handling code for your pollable, thus reducing its size and binary bloat. Since `wake()` is `noexcept`, our pollable above cannot throw, so we should rewrite it like this:

```cpp
struct ARC_NODISCARD Yield : arc::NoexceptPollable<Yield> {
    bool poll(arc::Context& cx) noexcept {
        if (!yielded) {
            yielded = true;
            cx.wake();
            return false;
        }
        return true;
    }

private:
    bool yielded = false;
};
```

Now, this definition is identical to the real `arc::Yield` :) (minus the added namespaces)

# PollableBase

**Note: this section is more advanced, and you likely don't need this until working with very low-level async concepts.** Feel free to read if you find it interesting though :)

This is the final and lowest level of pollables - structs that inherit `arc::PollableBase`. Every single thing that can be polled (including regular pollables and `arc::Future`) are derived from this class, and everything pollable that you want to create must also be. When inheriting `arc::Pollable`, all the dirty things are already done for you, but with this class you have to do the work of creating the vtable yourself. The vtable for pollables looks roughly like this:

```cpp
struct PollableVtable {
    using PollFn = bool(*)(void*, Context&) noexcept;
    using GetOutputFn = void(*)(void*, void* output);

    PollFn m_poll;
    GetOutputFn m_getOutput;
    const PollableMetadata* m_metadata;
};
```

Here, you may fill in these entries:
* Poll function - Advance the state, return whether the future is ready now (note that this has to always return a bool due to how C++ coroutines work). This function must not throw.
* Output function - This is how the pollable rethrows potential exceptions and actually returns the result, it must cast `output` to `arc::MaybeUninit<T>*` and initialize it on success. It can be null for void pollables.
* Metadata - Optional, can be kept as null but for debugging it's recommended to set it via `PollableMetadata::create<MyPollable>()`

Some important rules and things to keep in mind:
* The poll function may be called an undefined amount of times between the pollable being created and destroyed. It may be once, twice, zero times or a million. It may get called again without you ever registering a waker. You **must not** assume that `poll` being called means work is complete (e.g. mutex acquired or socket data received), instead you should always re-check.
* While optional, it is recommended to delegate logic to the `poll` method, instead of the pollable constructors. Constructors should usually be cheap and fast.
* `poll` should be effectively noexcept, and throwing from it is undefined behavior. `getOutput` is a good place for rethrowing, and this is exactly where `Future` and `Pollable` do it.
* Arc guarantees that in valid code, `poll` will **never** be called again if it returned `true`. It is UB to manually call `poll` on a pollable that already finished.
* Arc guarantees that `getOutput` will be called exactly once (if not null), right after `poll` returns `true`. It is UB to manually call `getOutput` more than once, or on a pollable that hasn't yet finished.
* Pollables are cancelled by destroying them. If you acquire some resource in your `poll` method, it's recommended to release it in the destructor, rather than when ready. This ensures proper cleanup is done if the pollable ever gets cancelled (for example by aborting the task, or when using `arc::timeout` / `arc::select`)