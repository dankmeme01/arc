#include <arc/prelude.hpp>
#include <gtest/gtest.h>

TEST(task, SpawnAwaitResult) {
    arc::Runtime runtime{1};

    int result = runtime.blockOn([] -> arc::Future<int> {
        auto task = arc::spawn([] -> arc::Future<int> {
            co_return 42;
        }());

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}

TEST(task, SpawnBlockingResult) {
    arc::Runtime runtime{1};

    int result = runtime.blockOn([] -> arc::Future<int> {
        auto task = arc::spawnBlocking<int>([] {
            return 42;
        });

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}
