#include "common.hpp"
#include <iostream>

Future<Result<>> asyncMain(int argc, const char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);

    if (args.size() != 1) {
        fmt::println("Usage: tcp_client <address>");
        co_return Ok();
    }

    auto address = args[0];
    ARC_CO_MAP_UNWRAP_INTO(auto addr, qsox::SocketAddress::parse(address));

    auto res = co_await TcpStream::connect(addr);
    if (!res) {
        fmt::println("Failed to connect: {}", res.unwrapErr());
        co_return Ok();
    }

    auto& stream = res.unwrap();

    while (true) {
        auto line = co_await spawnBlocking<std::string>([] {
            std::string line;
            std::cout << "> " << std::flush;
            std::getline(std::cin, line);
            return line;
        });

        if (line.empty()) {
            break;
        }

        ARC_CO_MAP_UNWRAP(co_await stream.sendAll(line.data(), line.size()));

        char buf[1024];
        ARC_CO_MAP_UNWRAP_INTO(size_t read, co_await stream.receive(buf, sizeof(buf)));

        fmt::println("Received {} bytes: {}", read, std::string_view(buf, read));
    }

    co_return Ok();
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);