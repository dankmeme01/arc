#include <arc/future/Future.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/task/Task.hpp>
#include <arc/task/Yield.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;
using namespace arc;

TEST(Future, ImmediateReturn) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut = [] -> Future<int> {
        co_return 123;
    }();

    EXPECT_TRUE(fut.poll(cx));
    EXPECT_EQ(fut.getOutput(), 123);
}

TEST(Future, YieldAndReturn) {
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

TEST(Future, Exception) {
    auto rt = Runtime::create(1);

    auto fut = [] -> Future<> {
        throw std::runtime_error("test exception");
        co_return;
    };

    EXPECT_THROW(rt->blockOn(fut()), std::runtime_error);
}

TEST(Future, ExceptionNested) {
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

TEST(Future, ExceptionNestedCatch) {
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
