/// Connects to a server and floods it with data. Useful for profiling the Arc networking stack & async in general

#include "common.hpp"
#include <iostream>

Future<Result<>> asyncMain(int argc, const char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);

    if (args.size() != 1) {
        fmt::println("Usage: tcp_data_flood <address>");
        co_return Ok();
    }

    auto address = args[0];
    ARC_CO_MAP_UNWRAP_INTO(auto addr, qsox::SocketAddress::parse(address));

    auto tres = co_await arc::timeout(asp::Duration::fromSecs(5), TcpStream::connect(addr));
    if (!tres) {
        fmt::println("Connection timed out!");
        co_return Ok();
    }

    auto res = std::move(tres).unwrap();
    if (!res) {
        fmt::println("Failed to connect: {}", res.unwrapErr());
        co_return Ok();
    }

    fmt::println("Connected to {}!", addr.toString());

    auto& stream = res.unwrap();

    std::vector<uint8_t> data;
    data.resize(1024 * 1024, 0x42);

    std::atomic<bool> done;
    auto h = arc::spawn([&] -> arc::Future<> {
        co_await ctrl_c();
        done = true;
    });

    size_t sent = 0;
    while (!done) {
        ARC_CO_MAP_UNWRAP(co_await stream.sendAll(data));
        sent += data.size();
        fmt::println("Sent {} byte chunk", data.size());
    }
    h.abort();

    fmt::println("Terminating after sending {} bytes", sent);

    co_return Ok();
}

ARC_DEFINE_MAIN_NT(asyncMain, 2);