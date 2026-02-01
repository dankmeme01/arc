#include <arc/future/Join.hpp>
#include <arc/sync/Notify.hpp>
#include <arc/sync/Semaphore.hpp>
#include <arc/sync/mpsc.hpp>
#include <arc/util/ManuallyDrop.hpp>
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
