#include <arc/future/UtilPollables.hpp>
#include <arc/time/Timeout.hpp>
#include <arc/time/Sleep.hpp>
#include <arc/time/Interval.hpp>
#include <gtest/gtest.h>

using enum std::memory_order;

using namespace arc;

TEST(Time, TimeoutHit) {
    auto rt = arc::Runtime::create(1);
    auto res1 = rt->blockOn(
        arc::timeout(asp::Duration::fromMillis(1), arc::never())
    );

    EXPECT_TRUE(res1.isErr());
}

TEST(Time, TimeoutNotHit) {
    auto rt = arc::Runtime::create(1);
    auto res1 = rt->blockOn(
        arc::timeout(asp::Duration::fromMillis(100), arc::yield())
    );

    EXPECT_TRUE(res1.isOk());
}

TEST(Time, ZeroTimeout) {
    auto rt = arc::Runtime::create(1);
    auto res1 = rt->blockOn(
        arc::timeout(asp::Duration::zero(), arc::yield())
    );

    EXPECT_TRUE(res1.isErr());
}


TEST(Time, TimeoutWithValue) {
    auto rt = arc::Runtime::create(1);
    auto res1 = rt->blockOn(
        arc::timeout(asp::Duration::fromMillis(1), arc::ready<int>(42))
    );

    EXPECT_TRUE(res1.isOk());
    EXPECT_EQ(res1.unwrap(), 42);
}

static Future<> throws() {
    throw std::runtime_error("failed");
    co_return;
}

TEST(Time, TimeoutException) {
    auto rt = arc::Runtime::create(1);

    EXPECT_THROW({
        (void) rt->blockOn(arc::timeout(asp::Duration::fromSecs(1), throws()));
    }, std::runtime_error);
}

TEST(Time, InfiniteDuration) {
    auto rt = arc::Runtime::create(1);

    auto ret = rt->blockOn(arc::timeout(asp::Duration::infinite(), arc::ready<int>(42)));
    EXPECT_TRUE(ret.isOk());
    EXPECT_EQ(ret.unwrap(), 42);
}

TEST(TimerQueue, DrainEmpty) {
    TimerQueue tq;
    auto vec = tq.drain();
    EXPECT_TRUE(vec.empty());
}

TEST(TimerQueue, InsertDrainOne) {
    TimerQueue tq;
    tq.insert(TimerEntry {
        .expiry = asp::Instant::now(),
        .waker = Waker::noop(),
        .id = 123,
    });
    auto vec = tq.drain();
    EXPECT_EQ(vec.size(), 1);
    EXPECT_EQ(vec[0].id, 123);
}

TEST(TimerQueue, InsertDrainOneFuture) {
    TimerQueue tq;
    tq.insert(TimerEntry {
        .expiry = asp::Instant::now() + asp::Duration::fromSecs(1),
        .waker = Waker::noop(),
        .id = 123,
    });
    auto vec = tq.drain();
    EXPECT_TRUE(vec.empty());
}

TEST(TimerQueue, InsertDrainMany) {
    size_t past = 5;
    size_t now = 5;
    size_t future = 5;
    size_t id = 0;
    TimerQueue tq;

    for (size_t i = 0; i < past; i++) {
        tq.insert(TimerEntry {
            .expiry = asp::Instant::now() - asp::Duration::fromSecs(1),
            .waker = Waker::noop(),
            .id = id++,
        });
    }

    for (size_t i = 0; i < now; i++) {
        tq.insert(TimerEntry {
            .expiry = asp::Instant::now(),
            .waker = Waker::noop(),
            .id = id++,
        });
    }

    for (size_t i = 0; i < future; i++) {
        tq.insert(TimerEntry {
            .expiry = asp::Instant::now() + asp::Duration::fromSecs(1),
            .waker = Waker::noop(),
            .id = id++,
        });
    }
}

TEST(TimerQueue, InsertErase) {
    TimerQueue tq;
    auto exp = asp::Instant::now() + asp::Duration::fromSecs(1);
    tq.insert(TimerEntry {
        .expiry = exp,
        .waker = Waker::noop(),
        .id = 123,
    });
    tq.erase(exp, 123);

    EXPECT_EQ(tq.drain().size(), 0);
}

TEST(TimerQueue, InsertEraseInvalid) {
    TimerQueue tq;
    auto exp = asp::Instant::now() + asp::Duration::fromSecs(1);
    EXPECT_NO_THROW(tq.erase(exp, 123));

    EXPECT_EQ(tq.drain().size(), 0);
}

