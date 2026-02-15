#include <arc/prelude.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(join, MpscMultiTxJoin) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(1);

    auto tfut1 = tx.send(1);
    auto tfut2 = tx.send(2);
    auto joined = arc::joinAll(std::move(tfut1), std::move(tfut2));

    EXPECT_FALSE(joined.poll(cx));

    auto recvval = rx.tryRecv();
    EXPECT_TRUE(recvval.isOk());
    EXPECT_EQ(recvval.unwrap(), 1);

    auto pollTwo = joined.poll(cx);
    EXPECT_TRUE(pollTwo.has_value());
    auto output = std::move(pollTwo).value();

    for (int i = 0; i < 2; i++) {
        EXPECT_TRUE(output[i].isOk());
    }
}

TEST(join, HeterogenousJoin) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut1 = [] -> arc::Future<int> {
        co_return 42;
    }();
    auto fut2 = arc::ready(42);

    auto joined = arc::joinAll(std::move(fut1), std::move(fut2));
    auto res = joined.poll(cx);
    EXPECT_TRUE(res.has_value());

    auto vec = std::move(res).value();
    EXPECT_EQ(vec[0], 42);
    EXPECT_EQ(vec[1], 42);
}

TEST(join, JoinDyn) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    arc::Notify notify;

    std::vector<Future<int>> futures;
    for (size_t i = 0; i < 5; i++) {
        futures.push_back([&](this auto self) -> Future<int> {
            co_await notify.notified();
            co_return 42;
        }());
    }

    auto joined = arc::joinAll(std::move(futures));
    EXPECT_FALSE(joined.poll(cx));

    notify.notifyAll();

    auto pollRes = joined.poll(cx);
    EXPECT_TRUE(pollRes.has_value());
    auto output = std::move(pollRes).value();
    EXPECT_EQ(output.size(), 5);
    for (const auto& val : output) {
        EXPECT_EQ(val, 42);
    }
}

TEST(join, JoinDynVoid) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    arc::Notify notify;

    std::vector<Future<>> futures;
    for (size_t i = 0; i < 5; i++) {
        futures.push_back([&](this auto self) -> Future<> {
            co_await notify.notified();
        }());
    }

    auto joined = arc::joinAll(std::move(futures));
    EXPECT_FALSE(joined.poll(cx));

    notify.notifyAll();

    auto pollRes = joined.poll(cx);
    EXPECT_TRUE(pollRes);
}

TEST(join, JoinAllExceptionFirst) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut1 = arc::pollFunc([] -> bool {
        throw std::runtime_error("failed");
    });
    auto fut2 = arc::pollFunc([] -> bool {
        return true;
    });

    auto ja = arc::joinAll(std::move(fut1), std::move(fut2));
    EXPECT_THROW(ja.poll(cx), std::runtime_error);
}

TEST(join, JoinAllExceptionSecond) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto fut1 = arc::pollFunc([] -> bool {
        return true;
    });
    auto fut2 = arc::pollFunc([] -> bool {
        throw std::runtime_error("failed");
    });

    auto ja = arc::joinAll(std::move(fut1), std::move(fut2));
    EXPECT_THROW(ja.poll(cx), std::runtime_error);
}

TEST(join, JoinAllDynExceptionFirst) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    std::vector<Future<>> futs;
    futs.push_back([] -> Future<> {
        throw std::runtime_error("failed");
        co_return;
    }());
    futs.push_back([] -> Future<> {
        co_return;
    }());

    auto ja = arc::joinAll(std::move(futs));
    EXPECT_THROW(ja.poll(cx), std::runtime_error);
}

TEST(join, JoinAllDynExceptionSecond) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    std::vector<Future<>> futs;
    futs.push_back([] -> Future<> {
        throw std::runtime_error("failed");
        co_return;
    }());
    futs.push_back([] -> Future<> {
        co_return;
    }());

    auto ja = arc::joinAll(std::move(futs));
    EXPECT_THROW(ja.poll(cx), std::runtime_error);
}

