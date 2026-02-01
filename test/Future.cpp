#include <arc/future/Future.hpp>
#include <arc/task/Task.hpp>
#include <arc/task/Yield.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;
using namespace arc;

TEST(future, Never) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto neverFut = arc::never();

    // never will always return false on poll
    EXPECT_FALSE(neverFut.poll(cx));
    EXPECT_FALSE(neverFut.poll(cx));
    EXPECT_FALSE(neverFut.poll(cx));
}

TEST(future, Yield) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto yieldFut = arc::yield();

    // first poll should return false
    EXPECT_FALSE(yieldFut.poll(cx));
    // second poll should return true
    EXPECT_TRUE(yieldFut.poll(cx));
    // subsequent polls should return true
    EXPECT_TRUE(yieldFut.poll(cx));
    EXPECT_TRUE(yieldFut.poll(cx));
}

TEST(future, PollFunc) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    int counter = 0;
    auto fut = arc::pollFunc([&] {
        counter++;
        return counter % 2 == 0;
    });

    EXPECT_FALSE(fut.poll(cx));
    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(fut.poll(cx));
    EXPECT_EQ(counter, 2);
    EXPECT_FALSE(fut.poll(cx));
    EXPECT_EQ(counter, 3);
    EXPECT_TRUE(fut.poll(cx));
    EXPECT_EQ(counter, 4);
}

TEST(future, ImmediateFuture) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut = [] -> Future<int> {
        co_return 123;
    }();

    EXPECT_TRUE(fut.poll(cx));
    EXPECT_EQ(fut.getOutput(cx), 123);
}

TEST(future, YieldingFuture) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut = [] -> Future<int> {
        co_await arc::yield();
        co_return 123;
    }();

    EXPECT_FALSE(fut.poll(cx));
    EXPECT_TRUE(fut.poll(cx));
    EXPECT_EQ(fut.getOutput(cx), 123);
}
