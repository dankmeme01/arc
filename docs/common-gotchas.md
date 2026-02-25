
# Common gotchas

This page is not entirely about Arc, but also about C++ coroutines in general.

## Reference parameteres in futures

Generally, references are *usually* OK if the object they are pointing to is **not temporary**. This means, `T&` is almost always fine, while `const T&` and `T&&` can be dangerous.

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

auto taskHandle = arc::spawn(getSize("string"));
co_await taskHandle;

auto ja = arc::joinAll(getSize("string"), getSize("string 2"));
co_await ja;
```

The examples earlier were ok, because when a temporary `std::string` is made, it exists up until the end of the statement. This includes the `co_await` expression, and even passing the future into other functions, such as `arc::spawn`, `arc::timeout`, `arc::joinAll`, etc. The other two examples store the future in a local variable, which leads to the temporary string being destroyed before the future is awaited.

As long as you make sure to not pass references to temporaries - you are completely safe to await other futures or pollables.

## Lifetimes of spawned tasks

When spawning tasks (i.e. using `arc::spawn`), you need to be **much more careful** about parameter lifetimes. In Rust frameworks like Tokio, spawning a future as a task requires it to be `'static`, meaning that it must capture *nothing* from the environment, except for things that will live forever. Arc requires the exact same constraint, since you have no guarantees about when a spawned task will finish running (it may never finish at all). Unfortunately, in C++ there is no good way to enforce or even detect that, which means mistakes like this are common:

```cpp
Future<> asyncFunc(const std::string& string) {
    fmt::println("String: {}", string);
    co_return;
}

void syncFunc() {
    std::string str = "hello";
    arc::spawn(asyncFunc(str));
}
```

In this example, `asyncFunc` is spawned in parallel and is almost always going to start running *after* `syncFunc` already returned. This leads to undefined behavior, as `asyncFunc` now captures variables local to the stack frame of `syncFunc`, which have already been destroyed.

There is no universal solution for this problem, but there are steps you can take to try and prevent most mistakes.

If you *creating* an asynchronous function:
* If you need to *store* arguments, take them as values. Avoid taking `const std::string&` or `std::string_view` if you will end up copying it into an `std::string` internally - just pass a string and `std::move` it.
* If you are writing a function that *might* often be spawned as a task rather than awaited directly, prefer also taking all arguments by value.
* If you are writing a public async API, it's a good idea to either defensively take every single argument by value, or to warn your users about the risk of spawning it.

If you are *calling* asynchronous functions:
* Check how they take their arguments - if it's by value then it's safe to spawn (`std::string`, `std::vector` but NOT `std::string_view`, `std::span`, `const std::string&`, etc.)
* If they take references or borrow data, it may be unsafe to spawn. You can work around it by making a wrapper lambda:

```cpp
void syncFunc() {
    std::string str = "hello";

    arc::spawn([str = std::move(str)] -> arc::Future<> {
        co_await asyncFunc(str);
    });
}
```

Now the string is moved into the task, and the task is no longer referencing any temporary variables, so this will be completely safe, regardless of how `asyncFunc` takes the argument.

## Lambda futures

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