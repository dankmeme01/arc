#include <arc/sync/mpsc.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(mpsc, Basic) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(3);

    EXPECT_TRUE(tx.trySend(1).isOk());
    auto fut1 = tx.send(2);
    EXPECT_TRUE(fut1.poll().has_value());
    EXPECT_TRUE(tx.trySend(3).isOk());
    EXPECT_FALSE(tx.trySend(4).isOk());

    // start fut2 and fut3 sends, should not complete yet
    auto fut2 = tx.send(5);
    auto fut3 = tx.send(6);
    EXPECT_FALSE(fut2.poll().has_value());
    EXPECT_FALSE(fut3.poll().has_value());

    // receive a value
    auto recv1 = rx.tryRecv();
    EXPECT_TRUE(recv1.isOk());
    EXPECT_EQ(recv1.unwrap(), 1);

    // poll fut3 and fut2
    EXPECT_TRUE(fut3.poll().has_value());
    EXPECT_FALSE(fut2.poll().has_value());

    // async receive
    auto futRecv = rx.recv();
    auto pres = futRecv.poll();
    EXPECT_TRUE(pres.has_value());
    EXPECT_TRUE(pres->isOk());
    EXPECT_EQ(pres->unwrap(), 2);

    // poll fut2 again
    EXPECT_TRUE(fut2.poll().has_value());

    // receive remaining values
    for (int expected : {3, 6, 5}) {
        auto r = rx.tryRecv();
        EXPECT_TRUE(r.isOk());
        EXPECT_EQ(r.unwrap(), expected);
    }

    EXPECT_FALSE(rx.tryRecv().isOk());
}
