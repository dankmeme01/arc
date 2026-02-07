#include "common.hpp"

Future<Result<>> clientHandler(arc::TcpStream stream, qsox::SocketAddress addr) {
    fmt::println("Accepted connection from {}", addr.toString());

    char buf[1024];
    while (true) {
        auto read = ARC_CO_MAP_UNWRAP(co_await stream.receive(buf, sizeof(buf)));

        fmt::println("Read {} bytes from {}: {}", read, addr.toString(), std::string_view(buf, read));

        ARC_CO_MAP_UNWRAP(co_await stream.sendAll(buf, read));
    }
}

Future<Result<>> asyncMain() {
    ARC_CO_MAP_UNWRAP_INTO(auto listener, co_await TcpListener::bind("0.0.0.0:59103"));

    fmt::println(
        "Listening for TCP connections on {}",
        listener.localAddress().unwrap().toString()
    );

    bool running = true;
    while (running) {
        co_await arc::select(
            arc::selectee(
                listener.accept(),
                [&](NetResult<std::pair<arc::TcpStream, qsox::SocketAddress>> res) {
                    if (!res) {
                        fmt::println("Error accepting connection: {}", res.unwrapErr());
                        running = false;
                        return;
                    }

                    auto [stream, addr] = std::move(res).unwrap();
                    arc::spawn([s = std::move(stream), addr] mutable -> arc::Future<> {
                        auto res = co_await clientHandler(std::move(s), addr);

                        if (!res) {
                            fmt::println("Client {} terminated: {}", addr.toString(), res.unwrapErr());
                        }
                    });
                }
            ),

            arc::selectee(arc::ctrl_c(), [&] { running = false; })
        );
    }

    co_return Ok();
}

ARC_DEFINE_MAIN_NT(asyncMain, 1);
