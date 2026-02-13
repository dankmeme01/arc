#include <arc/future/Future.hpp>
#include <arc/runtime/Runtime.hpp>
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

    // never can use various types
    auto lambda = [] -> arc::Future<int> {
        if (true) {
            co_return co_await arc::never<int>();
        } else {
            co_return 1;
        }
    };

    auto fut = lambda();
    EXPECT_FALSE(fut.poll(cx));
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
    EXPECT_EQ(fut.getOutput(), 123);
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
    EXPECT_EQ(fut.getOutput(), 123);
}

TEST(future, Exception) {
    auto rt = Runtime::create(1);

    auto fut = [] -> Future<> {
        throw std::runtime_error("test exception");
        co_return;
    };

    EXPECT_THROW(rt->blockOn(fut()), std::runtime_error);
}

TEST(future, ExceptionNested) {
    auto rt = Runtime::create(1);

    auto fut = [] -> Future<> {
        auto fut2 = [] -> Future<> {
            throw std::runtime_error("test exception");
        };

        co_await arc::yield();
        co_await fut2();
        co_await arc::yield();
    };

    EXPECT_THROW(rt->blockOn(fut()), std::runtime_error);
}

TEST(future, ExceptionNestedCatch) {
    auto rt = Runtime::create(1);

    auto fut = [] -> Future<> {
        auto fut2 = [] -> Future<> {
            throw std::runtime_error("test exception");
            co_return;
        };

        co_await arc::yield();
        try {
            co_await fut2();
        } catch (std::exception const& e) {
            EXPECT_STREQ(e.what(), "test exception");
        }
        co_await arc::yield();
    };

    EXPECT_NO_THROW(rt->blockOn(fut()));
}
