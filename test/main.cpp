// #include <arc/prelude.hpp>
#include <arc/prelude.hpp>
#include <arc/util/Result.hpp>
#include <asp/time.hpp>

#include <fmt/format.h>
#include <iostream>

#ifdef _WIN32
# include <Windows.h>
// Lol
# define pthread_self GetCurrentThreadId
#endif

using namespace asp::time;
using namespace arc;

#define DBG_FUT_WRAP(f) ({ \
    auto _fut = f; \
    _fut.setDebugName("# " #f); \
    std::move(_fut); \
})

#define dbg_await(f) ({ auto fut = DBG_FUT_WRAP(f); co_await fut; })
#define dbg_res_await(...) ARC_CO_UNWRAP((co_await (__VA_ARGS__)).mapErr([](auto err) { return fmt::format("Error: {}\nIn: {}", err.message(), #__VA_ARGS__); }))

Future<> recurseWait(int level) {
    if (level == 0) co_return;
    co_await(sleepFor(Duration::fromMillis(100)));
    trace("recurseWait level {} IN", level);
    dbg_await(recurseWait(level - 1));
    trace("recurseWait level {} OUT", level);
}

Future<int> waiter(Duration dur) {
    trace("{} is waiting for {}", pthread_self(), dur.toString());
    co_await sleepFor(dur);
    trace("{} has finished waiting for {}", pthread_self(), dur.toString());
    co_await arc::yield();
    co_return dur.millis() + 42;
}

Future<int> noop(int x) {
    if (x > 0) {
        co_await noop(x - 1);
    }
    co_return x;
}

Future<int> locker(Mutex<>& mtx) {
    trace("locker trying to lock mutex");
    auto guard = co_await mtx.lock();
    trace("locker acquired mutex");
    co_await sleepFor(Duration::fromMillis(500));
    trace("locker releasing mutex");
    co_return 478;
}

Future<Result<>> tcpTest() {
    auto stream = dbg_res_await(TcpStream::connect("45.79.112.203:4242"));
    fmt::println("Connected, sending data");

    char msg[] = "hello world\n";
    dbg_res_await(stream.send(msg, sizeof(msg)));
    fmt::println("Data sent, receiving");

    char buf[128] = {0};
    size_t n = dbg_res_await(stream.receive(buf, sizeof(buf) - 1));

    std::string_view received(buf, n);
    fmt::println("Received {} bytes: {}", n, received);
    co_return Ok();
}

Future<Result<>> tcpListenerTask(TcpStream stream, qsox::SocketAddress addr) {
    char buf[1024];
    while (true) {
        size_t n = dbg_res_await(stream.receive(buf, sizeof(buf)));

        if (n == 0) {
            fmt::println("Connection closed by {}", addr.toString());
            break;
        }

        dbg_res_await(stream.sendAll(buf, n));
    }

    co_return Ok();
}

Future<Result<>> tcpListener() {
    auto listener = dbg_res_await(TcpListener::bind(
        qsox::SocketAddress::parse("0.0.0.0:4242").unwrap()
    ));

    fmt::println("Listening for connections on {}..", listener.localAddress().unwrap().toString());

    bool running = true;
    while (running) {
        co_await arc::select(
            selectee(ctrl_c(), [&] {
                running = false;
            }),

            selectee(listener.accept(), [](auto res) {
                auto [stream, addr] = std::move(res).unwrap();
                fmt::println("Accepted connection from {}", addr.toString());

                spawn([](TcpStream s, qsox::SocketAddress a) mutable -> Future<> {
                    auto result = co_await tcpListenerTask(std::move(s), std::move(a));
                    if (!result) {
                        fmt::println("Connection error ({}): {}", a.toString(), result.unwrapErr());
                    }
                }(std::move(stream), addr));
            })
        );
    }

    co_return Ok();
}

#ifdef _WIN32

static auto PIPE_NAME = L"\\\\.\\pipe\\arc_iocp_test_pipe";

arc::Future<Result<>> pipeServer() {
    while (true) {
        auto pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,
            4096,
            NMPWAIT_USE_DEFAULT_WAIT,
            nullptr
        );

        trace("Listening for pipes ({})", pipe);
        auto p = ARC_CO_UNWRAP(co_await IocpPipe::listen(pipe));

        char buf[256];
        ARC_CO_UNWRAP_INTO(auto read, co_await p.read(buf, sizeof(buf)));
        fmt::println("Read {} bytes from pipe: {}", read, std::string_view(buf, read));

        ARC_CO_UNWRAP_INTO(auto written, co_await p.write(buf, read));
        fmt::println("Wrote {} bytes to pipe", written);
    }
}

Future<Result<>> iocpTest() {
    // listen in another thread
    arc::spawn([] -> arc::Future<> {
        auto res = co_await pipeServer();
        if (!res) {
            printWarn("Pipe server terminated: {}", res.unwrapErr());
        }
    });

    // interactive client
    while (true) {
        auto res = IocpPipe::open(PIPE_NAME);
        if (!res) {
            co_await arc::sleep(Duration::fromMillis(100));
            continue;
        }

        printWarn("Pipe opened");

        auto pipe = std::move(res).unwrap();
        std::cout << "> " << std::flush;
        std::string s;
        std::getline(std::cin, s);
        std::cout << std::endl;

        ARC_CO_UNWRAP(co_await pipe.write(s.data(), s.size()));
        char buf[256];

        trace("Reading into {}", (void*)buf);
        size_t readB = ARC_CO_UNWRAP(co_await pipe.read(buf, 256));

        fmt::println("Client received {} bytes: {}", readB, std::string_view{buf, readB});
    }

    co_return Ok();
}

#endif

Future<> printer() {
    size_t print = 0;

    while (true) {
        fmt::println("Printer: {}", print++);
        co_await sleepFor(Duration::fromMillis(100));
    }
}

Future<> noop() {
    co_return;
}

Future<> asyncMain(int argc, char** argv) {
    trace("Hello from asyncMain!");
    // spawn(printer());

    if (argc > 1) {
        std::string_view arg{argv[1]};

        if (arg == "tcp-server") {
            dbg_await(tcpListener()).unwrap();
            co_return;
        } else if (arg == "iocp-test") {
#ifdef _WIN32
            dbg_await(iocpTest()).unwrap();
            co_return;
#endif
        }
    }


    dbg_await(select(
        // future that finishes after 2.5 seconds
        selectee(
            sleepFor(Duration::fromMillis(2500)),
            []() { fmt::println("2.5 seconds elapsed, shutting down!"); }
        ),

        // future that never completes
        selectee(
            never(),
            []() { fmt::println("this will never happen"); }
        ),

        // future that waits for ctrl+c signal
        selectee(
            ctrl_c(),
            [] -> Future<> {
                fmt::println("Ctrl+C received, exiting!");
                co_return;
            }
        )
    ));

    co_return;
}

ARC_DEFINE_MAIN(asyncMain)