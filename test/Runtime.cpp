#include <arc/prelude.hpp>
#include <gtest/gtest.h>
#include <signal.h>

using namespace arc;



TEST(Runtime, DisabledTime) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH({
        auto rt = arc::Runtime::create(RuntimeOptions { .workers = 1, .timeDriver = false });
        rt->timeDriver();
    }, "");
}

TEST(Runtime, DisabledIo) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH({
        auto rt = arc::Runtime::create(RuntimeOptions { .workers = 1, .ioDriver = false });
        rt->ioDriver();
    }, "");
}

TEST(Runtime, DisabledSignal) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH({
        auto rt = arc::Runtime::create(RuntimeOptions { .workers = 1, .signalDriver = false });
        rt->signalDriver();
    }, "");
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

TEST(Runtime, MultiRuntimeMpsc) {
    auto rt1 = arc::Runtime::create(1);
    auto rt2 = arc::Runtime::create(1);

    auto [tx, rx] = arc::mpsc::channel<int>();

    rt1->spawn([tx = std::move(tx)] -> arc::Future<> {
        EXPECT_TRUE(co_await tx.send(42));
    }).blockOn();

    rt2->spawn([rx = std::move(rx)] mutable -> arc::Future<> {
        auto res = co_await rx.recv();
        EXPECT_TRUE(res);
        EXPECT_EQ(res.unwrap(), 42);
    }).blockOn();
}

#ifdef SIGUSR1

TEST(Runtime, MultiRuntimeSignal) {
    auto rt1 = arc::Runtime::create(1);
    auto rt2 = arc::Runtime::create(1);

    bool completed1 = false;
    bool completed2 = false;

    arc::Semaphore sem{0};

    auto h1 = rt1->spawn([&completed1, &sem] -> arc::Future<> {
        auto fut = arc::signal(SignalKind::User1);
        sem.release(1);

        co_await fut;
        completed1 = true;
    });

    auto h2 = rt2->spawn([&completed2, &sem] -> arc::Future<> {
        auto fut = arc::signal(SignalKind::User1);
        sem.release(1);

        co_await fut;
        completed2 = true;
    });


    sem.acquireBlocking(2);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});

    EXPECT_FALSE(completed1);
    EXPECT_FALSE(completed2);

    ::raise(SIGUSR1);

    h1.blockOn();
    h2.blockOn();

    EXPECT_TRUE(completed1);
    EXPECT_TRUE(completed2);
}

#endif
