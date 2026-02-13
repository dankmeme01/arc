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
