#include <arc/prelude.hpp>
#include <gtest/gtest.h>

using namespace arc;

TEST(Runtime, DisabledTime) {
    auto rt = arc::Runtime::create(1, false, true, true);
    Waker waker = Waker::noop();
    Context cx { &waker, rt.get() };

    EXPECT_THROW(rt->timeDriver(), std::exception);
    EXPECT_NO_THROW(rt->ioDriver());
    EXPECT_NO_THROW(rt->signalDriver());

    EXPECT_THROW((void)arc::sleep(asp::time::Duration::fromSecs(1)).poll(cx), std::exception);
}

TEST(Runtime, DisabledIo) {
    auto rt = arc::Runtime::create(1, true, false, true);
    Waker waker = Waker::noop();
    Context cx { &waker, rt.get() };

    EXPECT_THROW(rt->ioDriver(), std::exception);
    EXPECT_NO_THROW(rt->timeDriver());
    EXPECT_NO_THROW(rt->signalDriver());

    arc::setGlobalRuntime(rt.get());
    EXPECT_THROW((void) arc::UdpSocket::bindAny().poll(cx), std::exception);
}

TEST(Runtime, DisabledSignal) {
    auto rt = arc::Runtime::create(1, true, true, false);
    Waker waker = Waker::noop();
    Context cx { &waker, rt.get() };

    EXPECT_THROW(rt->signalDriver(), std::exception);
    EXPECT_NO_THROW(rt->timeDriver());
    EXPECT_NO_THROW(rt->ioDriver());

    EXPECT_THROW((void) arc::ctrl_c().poll(cx), std::exception);
}

TEST(Runtime, OutlivedSocket) {
    // in case the runtime is destroyed while we still have some resources like a socket,
    // we want to make sure there is no UB and the socket knows it's invalid
    std::optional<arc::UdpSocket> socket;

    {
        auto rt = arc::Runtime::create(1);

        socket.emplace(rt->blockOn(arc::UdpSocket::bindAny()).unwrap());
    }

    Waker waker = Waker::noop();
    Context cx { &waker, nullptr };

    // try to send data
    auto addr = qsox::SocketAddress::parse("127.0.0.1:8080").unwrap();
    auto fut = socket->sendTo("hello", 5, addr);
    EXPECT_FALSE(fut.poll(cx));

    arc::drop(std::move(socket));
}

TEST(Runtime, OutlivedTask) {
    arc::CancellationToken cancel{};
    {
        auto rt = arc::Runtime::create(1);
        rt->spawn([&cancel](this auto self) -> arc::Future<> {
            co_await cancel.waitCancelled();
        }());
    }

    cancel.cancel();
}

TEST(Runtime, ShutdownWithTasks) {
    auto rt = arc::Runtime::create(2);
    auto h1 = rt->spawn([] -> arc::Future<> {
        while (true) co_await arc::yield();
    }());
    rt->spawn([] -> arc::Future<> {
        while (true) co_await arc::sleep(asp::Duration::fromMillis(1));
    }());
    rt->safeShutdown();
}