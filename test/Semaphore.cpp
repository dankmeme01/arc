#include <arc/sync/Semaphore.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Semaphore, SingleAcquirer) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    Semaphore sem{6};
    EXPECT_TRUE(sem.tryAcquire(4));
    EXPECT_TRUE(sem.tryAcquire(2));
    EXPECT_FALSE(sem.tryAcquire(1));
    EXPECT_TRUE(sem.tryAcquire(0));

    sem.release(3);
    auto waiter1 = sem.acquire(5);
    EXPECT_FALSE(waiter1.poll());
    sem.release(3);
    EXPECT_TRUE(waiter1.poll());
}

TEST(Semaphore, MultipleAcquirers) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    Semaphore sem{10};

    auto w1 = sem.acquire(7);
    auto w2 = sem.acquire(9);
    auto w3 = sem.acquire(5);

    EXPECT_TRUE(w1.poll());
    EXPECT_FALSE(w2.poll());
    EXPECT_EQ(w2.remaining(), 6);
    EXPECT_EQ(sem.permits(), 0);
    EXPECT_FALSE(w3.poll());

    // release 5 permits
    sem.release(5);
    EXPECT_FALSE(w2.poll());
    EXPECT_EQ(w2.remaining(), 1);
    EXPECT_FALSE(w3.poll());

    sem.release(6);
    EXPECT_TRUE(w2.poll());
    EXPECT_TRUE(w3.poll());
    EXPECT_EQ(sem.permits(), 0);
}

TEST(Semaphore, DtorRelease) {
    Waker waker = Waker::noop();
    ctx().m_waker = &waker;

    Semaphore sem{5};

    {
        auto w1 = sem.acquire(10);
        EXPECT_FALSE(w1.poll()); // acquires 5/10 permits
        EXPECT_EQ(sem.permits(), 0);
    }

    EXPECT_EQ(sem.permits(), 5);
}

TEST(Semaphore, AcquireBlocking) {
    Semaphore sem{2};
    sem.acquireBlocking(2);

    EXPECT_FALSE(sem.tryAcquire(1));
}
