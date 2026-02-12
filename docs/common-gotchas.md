
# Common gotchas

This page is not entirely about Arc, but also about C++ coroutines in general.

## Reference parameteres in futures

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