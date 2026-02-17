#include <arc/prelude.hpp>
#include <gtest/gtest.h>

TEST(Task, SpawnAwaitResult) {
    auto runtime = arc::Runtime::create(1);

    int result = runtime->blockOn([] -> arc::Future<int> {
        auto task = arc::spawn([] -> arc::Future<int> {
            co_return 42;
        }());

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}

TEST(Task, SpawnBlockingResult) {
    auto runtime = arc::Runtime::create(1);

    int result = runtime->blockOn([] -> arc::Future<int> {
        auto task = arc::spawnBlocking<int>([] {
            return 42;
        });

        co_return co_await task;
    }());

    EXPECT_EQ(result, 42);
}

TEST(Task, BlockingBlockOn) {
    auto runtime = arc::Runtime::create(1);

    auto handle = runtime->spawnBlocking<int>([] {
        return 42;
    });
    int result = handle.blockOn();

    EXPECT_EQ(result, 42);
}

TEST(Task, LambdaTask) {
    auto runtime = arc::Runtime::create(1);

    runtime->spawn([] -> arc::Future<> {
        auto result = co_await arc::spawn([] -> arc::Future<int> {
            co_return 42;
        });
        EXPECT_EQ(result, 42);
    });
}

TEST(Task, DanglingTask) {
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


TEST(Task, TaskStats) {
    auto rt = arc::Runtime::create(1);

    auto handle = rt->spawn([] -> arc::Future<> {
        co_await arc::yield();
        co_return;
    }());
    handle.setName("hi test");

#ifdef ARC_DEBUG
    auto data = handle.getDebugData();
    EXPECT_TRUE((bool) data);

    auto allStats = rt->getTaskStats();
    EXPECT_EQ(allStats.size(), 1);
    EXPECT_EQ(allStats[0]->name(), "hi test");

    handle.blockOn();

    EXPECT_EQ(data->name(), "hi test");
    EXPECT_EQ(data->totalPolls(), 2); // polled once to yield, second to complete
#endif
}

TEST(Task, LambdaUseAfterFree) {
    auto rt = arc::Runtime::create(1);

    auto sptr = asp::make_shared<int>(0);
    EXPECT_EQ(sptr.strongCount(), 1);

    auto handle = rt->spawn(
        [sptr]() -> arc::Future<> {
            co_await arc::sleep(asp::Duration::fromMillis(1));
            EXPECT_EQ(sptr.strongCount(), 2);
        }
    );
    handle.blockOn();

    // since the task terminated, the lambda should be dropped
    EXPECT_EQ(sptr.strongCount(), 1);
}

TEST(Task, NullHandle) {
    arc::TaskHandle<int> handle;
    EXPECT_FALSE(handle);
    EXPECT_FALSE(handle.isValid());
    EXPECT_THROW(handle.blockOn(), std::runtime_error);
}

TEST(Task, Detach) {
    auto runtime = arc::Runtime::create(1);
    arc::TaskHandle<int> handle = runtime->spawn([] -> arc::Future<int> {
        co_await arc::never();
        co_return 42;
    });

    EXPECT_TRUE(handle.isValid());
    handle.detach();
    EXPECT_FALSE(handle.isValid());
}

TEST(Task, ThrowingTaskBlockOn) {
    auto runtime = arc::Runtime::create(1);
    auto handle = runtime->spawn([] -> arc::Future<int> {
        co_await arc::yield();
        throw std::runtime_error("test error");
        co_return 42;
    });

    EXPECT_THROW(handle.blockOn(), std::runtime_error);
}

TEST(Task, ThrowingTaskIgnore) {
    auto runtime = arc::Runtime::create(1);

    bool terminated = false;
    runtime->setTerminateHandler([&](auto) {
        terminated = true;
    });

    arc::Semaphore sema{0};

    auto handle = runtime->spawn([&] -> arc::Future<int> {
        auto _dtor = arc::scopeDtor([&] {
            sema.release();
        });

        co_await arc::yield();
        throw std::runtime_error("test error");
        co_return 42;
    });

    sema.acquireBlocking();

    // this is just to assert that there is no crash or anything, since exception should just be logged non fatally
    EXPECT_FALSE(terminated);
}
