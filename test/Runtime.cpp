#include <arc/prelude.hpp>
#include <gtest/gtest.h>

TEST(Runtime, DisabledTime) {
    auto rt = arc::Runtime::create(1, false, true, true);
    arc::ctx().m_runtime = rt.get();

    EXPECT_THROW(rt->timeDriver(), std::exception);
    EXPECT_NO_THROW(rt->ioDriver());
    EXPECT_NO_THROW(rt->signalDriver());

    EXPECT_THROW((void)arc::sleep(asp::time::Duration::fromSecs(1)).poll(), std::exception);
}

TEST(Runtime, DisabledIo) {
    auto rt = arc::Runtime::create(1, true, false, true);
    arc::ctx().m_runtime = rt.get();

    EXPECT_THROW(rt->ioDriver(), std::exception);
    EXPECT_NO_THROW(rt->timeDriver());
    EXPECT_NO_THROW(rt->signalDriver());

    EXPECT_THROW((void) arc::UdpSocket::bindAny().poll(), std::exception);
}

TEST(Runtime, DisabledSignal) {
    auto rt = arc::Runtime::create(1, true, true, false);
    arc::ctx().m_runtime = rt.get();

    EXPECT_THROW(rt->signalDriver(), std::exception);
    EXPECT_NO_THROW(rt->timeDriver());
    EXPECT_NO_THROW(rt->ioDriver());

    EXPECT_THROW((void) arc::ctrl_c().poll(), std::exception);
}

TEST(Runtime, OutlivedSocket) {
    // in case the runtime is destroyed while we still have some resources like a socket,
    // we want to make sure there is no UB and the socket knows it's invalid
    std::optional<arc::UdpSocket> socket;

    {
        auto rt = arc::Runtime::create(1);
        arc::ctx().m_runtime = rt.get();

        socket.emplace(rt->blockOn(arc::UdpSocket::bindAny()).unwrap());
    }

    // try to send data
    auto addr = qsox::SocketAddress::parse("127.0.0.1:8080").unwrap();
    auto fut = socket->sendTo("hello", 5, addr);
    EXPECT_FALSE(fut.poll());

    arc::drop(std::move(socket));
}

TEST(Runtime, OutlivedTask) {
    arc::CancellationToken cancel{};
    {
        auto rt = arc::Runtime::create(1);
        arc::ctx().m_runtime = rt.get();

        rt->spawn([&cancel](this auto self) -> arc::Future<> {
            co_await cancel.waitCancelled();
        }());
    }

    cancel.cancel();
}