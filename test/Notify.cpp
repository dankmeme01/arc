#include <arc/sync/Notify.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Notify, SingleAwaiter) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    arc::Notify notify;
    auto waiter = notify.notified();
    EXPECT_FALSE(waiter.poll());
    notify.notifyOne();
    EXPECT_TRUE(waiter.poll());
}

TEST(Notify, StoredPermit) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    arc::Notify notify;
    notify.notifyOne();

    auto waiter = notify.notified();
    EXPECT_TRUE(waiter.poll());
}

TEST(Notify, MultipleWaiters) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    arc::Notify notify;
    auto w1 = notify.notified();
    auto w2 = notify.notified();
    auto w3 = notify.notified();

    EXPECT_FALSE(w1.poll());
    EXPECT_FALSE(w2.poll());
    EXPECT_FALSE(w3.poll());

    notify.notifyOne();
    EXPECT_TRUE(w1.poll());
    EXPECT_FALSE(w2.poll());
    EXPECT_FALSE(w3.poll());

    notify.notifyAll();
    EXPECT_TRUE(w2.poll());
    EXPECT_TRUE(w3.poll());
}
