# Task Local Data

Task local data is a feature introduced in Arc v1.6, which allows you to create global variables that have separate contents in different tasks, similar to the `thread_local` feature of C++, but for tasks instead of threads.

To declare a task local variable, create a global/static variable wrapped in `arc::TaskLocalKey` and call `awaiter()` to get a reference.
```cpp
arc::Future<> myFuture() {
    // this MUST be static or global, otherwise it's treated like a regular local variable,
    // in which case the behavior will be different
    static arc::TaskLocalKey<int> s_value;

    // obtain a reference by awaiting it
    int& value = co_await s_value.awaiter();

    value = 123;
}
```

The returned reference stays valid and stable until the task is destroyed, and the object is heap allocated. Every task spawned with `arc::spawn` has its private copy of the object that does not affect others. This means the same task local variable can be used to share state between different functions or different function invocations of the same task.

Whenever you need some object to exist once per task, it is highly recommended to use `TaskLocalKey`. Other solutions like `thread_local` also work and ensure exclusive access, but can introduce surprises since the same coroutine may be ran by different threads between suspension points.
