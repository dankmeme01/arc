#include <arc/prelude.hpp>
#include <gtest/gtest.h>

using namespace arc;

TEST(Select, ReadyAndNever) {
    auto rt = Runtime::create(1);

    bool calledReady = false;
    rt->blockOn(
        arc::select(
            arc::selectee(arc::ready(), [&] {
                calledReady = true;
            }),
            arc::selectee(arc::never(), [] {
                EXPECT_TRUE(false);
            })
        )
    );

    EXPECT_TRUE(calledReady);
}

TEST(Select, DisabledBranch) {
    auto rt = Runtime::create(1);

    bool calledReady = false;
    bool calledYield = false;

    rt->blockOn(
        arc::select(
            arc::selectee(arc::ready(), [&] {
                calledReady = true;
            }, false),
            arc::selectee(arc::yield(), [&] {
                calledYield = true;
            })
        )
    );

    EXPECT_FALSE(calledReady);
    EXPECT_TRUE(calledYield);
}

TEST(Select, TwoReady) {
    auto rt = Runtime::create(1);

    bool called1 = false;
    bool called2 = false;

    rt->blockOn(
        arc::select(
            arc::selectee(arc::ready(), [&] {
                called1 = true;
            }),
            arc::selectee(arc::ready(), [&] {
                called2 = true;
            })
        )
    );

    // currently the order of select is deterministic, so the first should always complete first
    EXPECT_TRUE(called1);
    EXPECT_FALSE(called2);
}

arc::Future<> throws() {
    throw std::runtime_error("failed");
    co_return;
}

TEST(Select, Exception) {
    auto rt = Runtime::create(1);

    EXPECT_THROW({
        rt->blockOn(
            arc::select(
                arc::selectee(throws()),
                arc::selectee(arc::ready())
            )
        );
    }, std::runtime_error);
}

TEST(Select, ExceptionFinishedLast) {
    auto rt = Runtime::create(1);

    EXPECT_NO_THROW({
        rt->blockOn(
            arc::select(
                arc::selectee(arc::ready()),
                arc::selectee(throws())
            )
        );
    });
}
