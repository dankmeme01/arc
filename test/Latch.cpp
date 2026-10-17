#include <arc/sync/Latch.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Latch, SingleWaiter) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    Latch latch{3};

    auto awaiter = latch.wait();
    EXPECT_FALSE(awaiter.poll(cx));

    latch.countDown(2);
    EXPECT_FALSE(awaiter.poll(cx));

    latch.countDown(1);
    EXPECT_TRUE(awaiter.poll(cx));
}

TEST(Latch, MultipleWaiters) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    Latch latch{5};

    auto awaiter1 = latch.wait();
    auto awaiter2 = latch.wait();

    EXPECT_FALSE(awaiter1.poll(cx));
    EXPECT_FALSE(awaiter2.poll(cx));

    latch.countDown(3);
    EXPECT_FALSE(awaiter1.poll(cx));
    EXPECT_FALSE(awaiter2.poll(cx));

    latch.countDown(2);
    EXPECT_TRUE(awaiter1.poll(cx));
    EXPECT_TRUE(awaiter2.poll(cx));
}

TEST(Latch, AsyncWaiters) {
    auto rt = arc::Runtime::create(1);

    Latch latch{4};

    bool completed1 = false;
    bool completed2 = false;

    auto h1 = rt->spawn([&] -> arc::Future<> {
        co_await latch.wait();
        completed1 = true;
    });

    auto h2 = rt->spawn([&] -> arc::Future<> {
        co_await latch.wait();
        completed2 = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_FALSE(completed1);
    EXPECT_FALSE(completed2);

    latch.countDown(4);

    h1.blockOn();
    h2.blockOn();

    EXPECT_TRUE(completed1);
    EXPECT_TRUE(completed2);
}