#include <arc/prelude.hpp>
#include <gtest/gtest.h>

TEST(task, SpawnAwaitResult) {
    auto runtime = arc::Runtime::create(1);

    int result = runtime->blockOn([] -> arc::Future<int> {
        auto task = arc::spawn([] -> arc::Future<int> {
            co_return 42;
        }());

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}

TEST(task, SpawnBlockingResult) {
    auto runtime = arc::Runtime::create(1);

    int result = runtime->blockOn([] -> arc::Future<int> {
        auto task = arc::spawnBlocking<int>([] {
            return 42;
        });

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}

TEST(task, BlockingBlockOn) {
    auto runtime = arc::Runtime::create(1);

    auto handle = runtime->spawnBlocking<int>([] {
        return 42;
    });
    int result = handle.blockOn();

    EXPECT_EQ(result, 42);
}

TEST(task, LambdaTask) {
    auto runtime = arc::Runtime::create(1);


    runtime->spawn([] -> arc::Future<> {
        auto result = co_await arc::spawn([] -> arc::Future<int> {
            co_return 42;
        });
        EXPECT_EQ(result, 42);
    });
}

TEST(task, DanglingTask) {
    std::optional<arc::TaskHandle<void>> h;
    {
        auto runtime = arc::Runtime::create(1);

        h = runtime->spawn([] -> arc::Future<> {
            co_await arc::sleep(asp::Duration::fromDays(1));
            co_return;
        }());
    }

    // this would segfault if runtime freed the task
    h->abort();
}