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
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(1);

    auto tfut1 = tx.send(1);
    auto tfut2 = tx.send(2);
    auto joined = arc::joinAll(std::move(tfut1), std::move(tfut2));

    EXPECT_FALSE(joined.poll());

    auto recvval = rx.tryRecv();
    EXPECT_TRUE(recvval.isOk());
    EXPECT_EQ(recvval.unwrap(), 1);

    auto pollTwo = joined.poll();
    EXPECT_TRUE(pollTwo.has_value());
    auto output = std::move(pollTwo).value();

    for (int i = 0; i < 2; i++) {
        EXPECT_TRUE(output[i].isOk());
    }

}
