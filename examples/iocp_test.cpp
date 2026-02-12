#include "common.hpp"

#ifdef _WIN32

#include <iostream>
#include <Windows.h>

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

Future<Result<>> asyncMain() {
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

#else

Future<Result<>> asyncMain() { co_return Err("This example only works on Windows"); }

#endif

ARC_DEFINE_MAIN_NT(asyncMain, 1);
