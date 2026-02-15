#include <arc/sync/mpsc.hpp>
#include <arc/task/Yield.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/ManuallyDrop.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(mpsc, VeryBasicSync) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(3);

    trace("1");
    EXPECT_TRUE(tx.trySend(1));
    trace("2");
    EXPECT_TRUE(tx.trySend(2));
    trace("3");
    EXPECT_TRUE(tx.trySend(3));
    trace("4");

    auto r1 = rx.tryRecv();
    trace("5");
    EXPECT_TRUE(r1.isOk());
    EXPECT_EQ(r1.unwrap(), 1);

    trace("6");
    auto r2 = rx.tryRecv();
    trace("7");
    EXPECT_TRUE(r2.isOk());
    EXPECT_EQ(r2.unwrap(), 2);

    trace("8");
    auto r3 = rx.tryRecv();
    EXPECT_TRUE(r3.isOk());
    EXPECT_EQ(r3.unwrap(), 3);
    trace("9");

    auto r4 = rx.tryRecv();
    EXPECT_FALSE(r4.isOk());
    trace("10");
}

TEST(mpsc, Basic) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(3);

    EXPECT_TRUE(tx.trySend(1).isOk());
    auto fut1 = tx.send(2);
    EXPECT_TRUE(fut1.poll(cx).has_value());
    EXPECT_TRUE(tx.trySend(3).isOk());
    EXPECT_FALSE(tx.trySend(4).isOk());

    // start fut2 and fut3 sends, should not complete yet
    auto fut2 = tx.send(5);
    auto fut3 = tx.send(6);
    EXPECT_FALSE(fut2.poll(cx).has_value());
    EXPECT_FALSE(fut3.poll(cx).has_value());

    // receive a value
    auto recv1 = rx.tryRecv();
    EXPECT_TRUE(recv1.isOk());
    EXPECT_EQ(recv1.unwrap(), 1);

    // poll fut2 and fut3
    EXPECT_TRUE(fut2.poll(cx).has_value());
    EXPECT_FALSE(fut3.poll(cx).has_value());

    // async receive
    auto futRecv = rx.recv();
    auto pres = futRecv.poll(cx);
    EXPECT_TRUE(pres.has_value());
    EXPECT_TRUE(pres->isOk());
    EXPECT_EQ(pres->unwrap(), 2);

    // poll fut3 again
    EXPECT_TRUE(fut3.poll(cx).has_value());

    // receive remaining values
    for (int expected : {3, 5, 6}) {
        auto r = rx.tryRecv();
        EXPECT_TRUE(r.isOk());
        EXPECT_EQ(r.unwrap(), expected);
    }

    EXPECT_FALSE(rx.tryRecv().isOk());
}

TEST(mpsc, ClosedBySender) {
    Waker waker = Waker::noop();
    Context cx { &waker };

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
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(2);
    EXPECT_TRUE(tx.trySend(1).isOk());
    EXPECT_TRUE(tx.trySend(2).isOk());
    auto tx2 = tx;
    auto txsend = tx.send(3);
    auto txsend2 = tx2.send(4);
    EXPECT_FALSE(txsend.poll(cx).has_value());
    EXPECT_FALSE(txsend2.poll(cx).has_value());

    EXPECT_EQ(rx.tryRecv().unwrap(), 1);

    // now first send will succeed but not the second
    EXPECT_TRUE(txsend.poll(cx).has_value());
    EXPECT_FALSE(txsend2.poll(cx).has_value());

    // drop the receiver
    arc::drop(std::move(rx));

    // any pending and future sends should now fail
    auto p = txsend2.poll(cx);
    EXPECT_TRUE(p.has_value());
    EXPECT_TRUE(p->isErr());
    EXPECT_EQ(p->unwrapErr(), 4);

    auto tr = tx.trySend(5);
    EXPECT_FALSE(tr.isOk());
    EXPECT_EQ(tr.unwrapErr(), 5);
}

TEST(mpsc, Unbounded) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(std::nullopt);

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
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(0);

    // try_send/send should fail/block when there's no receiver
    EXPECT_FALSE(tx.trySend(1).isOk());
    auto futsend = tx.send(42);
    EXPECT_FALSE(futsend.poll(cx).has_value());

    // try to receive now
    auto futrecv = rx.recv();
    auto rpoll = futrecv.poll(cx);
    EXPECT_TRUE(rpoll.has_value());
    EXPECT_TRUE(rpoll->isOk());
    EXPECT_EQ(rpoll->unwrap(), 42);

    // now the send is also completed
    auto spoll = futsend.poll(cx);
    EXPECT_TRUE(spoll.has_value());
    EXPECT_TRUE(spoll->isOk());
}

void moveFutureHelper(auto& cx, auto txfut, auto rxfut) {
    // tx fails to send because no receiver
    EXPECT_FALSE(txfut.poll(cx).has_value());
    // rx succeeds by taking from sender
    EXPECT_TRUE(rxfut.poll(cx).has_value());
    EXPECT_TRUE(txfut.poll(cx).has_value());
}

TEST(mpsc, MoveFuture) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = mpsc::channel<int>(0);
    auto futsend = tx.send(123);
    auto futrecv = rx.recv();
    moveFutureHelper(cx, std::move(futsend), std::move(futrecv));
}

TEST(mpsc, LargeVolumeSmallChannel) {
    auto rt = arc::Runtime::create(4);
    auto [tx, rx] = mpsc::channel<int>(8);
    auto [outTx, outRx] = mpsc::channel<uint64_t>(1);


    auto [a, b] = rt->blockOn([&] -> arc::Future<std::pair<uint64_t, uint64_t>> {
        // spawn consumer thread
        arc::spawn([outTx, rx = std::move(rx)] -> arc::Future<> {
            uint64_t sum = 0;

            while (true) {
                auto res = co_await rx.recv();
                if (!res) break;
                sum += *res;
                co_await arc::yield();
            }

            EXPECT_TRUE((co_await outTx.send(sum)).isOk());
        });

        // produce a large amount of data
        uint64_t actualSum = 0;
        for (int i = 0; i < 4096; i++) {
            EXPECT_TRUE((co_await tx.send(i)).isOk());
            actualSum += i;
        }

        arc::drop(std::move(tx)); // this should close the channel

        auto taskSum = co_await outRx.recv();
        EXPECT_TRUE(taskSum.isOk());
        co_return std::make_pair(actualSum, *taskSum);
    });

    EXPECT_EQ(a, b);
}


TEST(mpsc, LargeVolumeLargeChannel) {
    auto rt = arc::Runtime::create(4);
    auto [tx, rx] = mpsc::channel<int>(8192);
    auto [outTx, outRx] = mpsc::channel<uint64_t>(1);

    // produce a large amount of data
    auto [a, b] = rt->blockOn([] -> arc::Future<std::pair<uint64_t, uint64_t>> {
        // spawn consumer thread
        arc::spawn([outTx, rx = std::move(rx)](this auto self) -> arc::Future<> {
            uint64_t sum = 0;

            for (size_t counter = 1;; counter++) {
                auto res = co_await rx.recv();
                if (!res) break;
                sum += *res;
                co_await arc::yield();
            }
            trace("consumer done");

            EXPECT_TRUE((co_await outTx.send(sum)).isOk());
        });

        uint64_t actualSum = 0;
        for (int i = 0; i < 4096; i++) {
            EXPECT_TRUE((co_await tx.send(i)).isOk());
            actualSum += i;
        }

        arc::drop(std::move(tx)); // this should close the channel

        auto taskSum = co_await outRx.recv();
        EXPECT_TRUE(taskSum.isOk());
        co_return std::make_pair(actualSum, *taskSum);
    });

    EXPECT_EQ(a, b);
}
