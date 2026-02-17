#include <arc/prelude.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Join, MpscMultipleSenders) {
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

TEST(Join, Heterogenous) {
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

TEST(Join, ExceptionFirst) {
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

TEST(Join, ExceptionSecond) {
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

TEST(Join, EmptyVoid) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto joined = arc::joinAll();
    auto res = joined.poll(cx);
    EXPECT_TRUE(res.has_value());
    auto val = std::move(res).value();
    static_assert(std::is_same_v<decltype(val), std::array<std::monostate, 0>>);
    EXPECT_EQ(val.size(), 0);
}

// Dynamic join (arc::joinAll(container<F>))

TEST(JoinDyn, FiveFutures) {
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

TEST(JoinDyn, FiveVoidFutures) {
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

TEST(JoinDyn, ExceptionFirst) {
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

TEST(JoinDyn, ExceptionSecond) {
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

TEST(JoinDyn, EmptyVoid) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    std::vector<Future<>> futs;

    auto joined = arc::joinAll(std::move(futs));
    auto res = joined.poll(cx);
    EXPECT_TRUE(res);
}

TEST(JoinDyn, EmptyNonVoid) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    std::vector<Future<int>> futs;

    auto joined = arc::joinAll(std::move(futs));
    auto res = joined.poll(cx);
    EXPECT_TRUE(res.has_value());
    auto vec = std::move(res).value();
    EXPECT_TRUE(vec.empty());
}