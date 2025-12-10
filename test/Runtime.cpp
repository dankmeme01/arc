#include <arc/prelude.hpp>
#include <gtest/gtest.h>

TEST(Runtime, DisabledTime) {
    arc::Runtime rt(1, false, true, true);
    arc::ctx().m_runtime = &rt;

    EXPECT_THROW(rt.timeDriver(), std::exception);
    EXPECT_NO_THROW(rt.ioDriver());
    EXPECT_NO_THROW(rt.signalDriver());

    EXPECT_THROW((void)arc::sleep(asp::time::Duration::fromSecs(1)).poll(), std::exception);
}

TEST(Runtime, DisabledIo) {
    arc::Runtime rt(1, true, false, true);
    arc::ctx().m_runtime = &rt;

    EXPECT_THROW(rt.ioDriver(), std::exception);
    EXPECT_NO_THROW(rt.timeDriver());
    EXPECT_NO_THROW(rt.signalDriver());

    EXPECT_THROW((void) arc::UdpSocket::bindAny().poll(), std::exception);
}

TEST(Runtime, DisabledSignal) {
    arc::Runtime rt(1, true, true, false);
    arc::ctx().m_runtime = &rt;

    EXPECT_THROW(rt.signalDriver(), std::exception);
    EXPECT_NO_THROW(rt.timeDriver());
    EXPECT_NO_THROW(rt.ioDriver());

    EXPECT_THROW((void) arc::ctrl_c().poll(), std::exception);
}