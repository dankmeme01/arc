#include <arc/sync/mpsc.hpp>
#include <arc/util/ManuallyDrop.hpp>
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


TEST(mpsc, ClosedBySender) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(2);
    EXPECT_TRUE(tx.trySend(1).isOk());
    EXPECT_TRUE(tx.trySend(2).isOk());
    // drop the sender, causing a closure since there are no more senders
    arc::drop(std::move(tx));

    // rx should be able to receive the two values
    EXPECT_EQ(rx.tryRecv().unwrap(), 1);
    EXPECT_EQ(rx.tryRecv().unwrap(), 2);

    // further receives should indicate closure
    auto r = rx.tryRecv();
    EXPECT_FALSE(r.isOk());
    EXPECT_TRUE(r.unwrapErr() == mpsc::TryRecvOutcome::Closed);
}


TEST(mpsc, ClosedByReceiver) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(2);
    EXPECT_TRUE(tx.trySend(1).isOk());
    EXPECT_TRUE(tx.trySend(2).isOk());
    auto tx2 = tx;
    auto txsend = tx.send(3);
    EXPECT_FALSE(txsend.poll().has_value());

    EXPECT_EQ(rx.tryRecv().unwrap(), 1);
    // drop the receiver
    arc::drop(std::move(rx));

    // any pending and future sends should fail despite there being space
    auto p = txsend.poll();
    EXPECT_TRUE(p.has_value());
    EXPECT_TRUE(p->isErr());
    EXPECT_EQ(p->unwrapErr(), 3);

    auto tr = tx.trySend(4);
    EXPECT_FALSE(tr.isOk());
    EXPECT_EQ(tr.unwrapErr(), 4);
}

TEST(mpsc, ZeroCapacity) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(0);

    for (size_t i = 0; i < 128; i++) {
        EXPECT_TRUE(tx.trySend(i).isOk());
    }

    for (size_t i = 0; i < 128; i++) {
        auto r = rx.tryRecv();
        EXPECT_TRUE(r.isOk());
        EXPECT_EQ(r.unwrap(), i);
    }
}

TEST(mpsc, Rendezvous) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    auto [tx, rx] = mpsc::channel<int>(std::nullopt);

    // try_send/send should fail/block when there's no receiver
    EXPECT_FALSE(tx.trySend(1).isOk());
    auto futsend = tx.send(42);
    EXPECT_FALSE(futsend.poll().has_value());

    // try to receive
    auto futrecv = rx.recv();
    EXPECT_FALSE(futrecv.poll().has_value());

    // now the send should complete
    auto spoll = futsend.poll();
    EXPECT_TRUE(spoll.has_value());
    EXPECT_TRUE(spoll->isOk());

    auto rpoll = futrecv.poll();
    EXPECT_TRUE(rpoll.has_value());
    EXPECT_TRUE(rpoll->isOk());
    EXPECT_EQ(rpoll->unwrap(), 42);
}