#include <arc/prelude.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;
using namespace arc;

TEST(Pollable, Never) {
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

TEST(Pollable, Yield) {
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

TEST(Pollable, PollFunc) {
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
