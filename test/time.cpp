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


