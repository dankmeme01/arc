#include <arc/sync/oneshot.hpp>
#include <arc/task/Yield.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/ManuallyDrop.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Oneshot, VeryBasic) {
    auto [tx, rx] = oneshot::channel<int>();
    EXPECT_TRUE(tx.send(42).isOk());

    auto r1 = rx.tryRecv();
    EXPECT_TRUE(r1.isOk());
    EXPECT_EQ(r1.unwrap(), 42);
}

TEST(Oneshot, VeryBasicAsync) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = oneshot::channel<int>();
    auto rfut = rx.recv();
    EXPECT_FALSE(rfut.poll(cx).has_value());
    EXPECT_TRUE(tx.send(42).isOk());
    auto val = rfut.poll(cx);
    EXPECT_TRUE(val.has_value());
    EXPECT_TRUE(val->isOk());
    EXPECT_EQ(val->unwrap(), 42);
}

TEST(Oneshot, SendAfterClosure) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = oneshot::channel<int>();
    arc::drop(std::move(rx));
    auto sendres = tx.send(42);
    EXPECT_FALSE(sendres.isOk());
    EXPECT_EQ(sendres.unwrapErr(), 42);
}

TEST(Oneshot, RecvAfterClosure) {
    Waker waker = Waker::noop();
    Context cx { &waker };

    auto [tx, rx] = oneshot::channel<int>();
    arc::drop(std::move(tx));
    auto recvres = rx.tryRecv();
    EXPECT_FALSE(recvres.isOk());
    EXPECT_TRUE(recvres.unwrapErr() == chan::TryRecvOutcome::Closed);

    auto recvfut = rx.recv();
    auto pres = recvfut.poll(cx);
    EXPECT_TRUE(pres.has_value());
    EXPECT_TRUE(pres->isErr());
}

