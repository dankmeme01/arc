#include <arc/net/TcpListener.hpp>
#include <arc/util/Result.hpp>

using namespace qsox;

namespace arc {

TcpListener::~TcpListener() {
    // deregister io before destroying the socket
    this->unregister();
}

TcpListener TcpListener::fromQsox(qsox::TcpListener listener) {
    (void) listener.setNonBlocking(true);
    auto rio = Runtime::current()->ioDriver().registerIo(listener.handle(), Interest::Readable);
    return TcpListener{std::move(listener), std::move(rio)};
}

Future<NetResult<TcpListener>> TcpListener::bind(const qsox::SocketAddress& address) {
    ARC_CO_UNWRAP_INTO(auto listener, qsox::TcpListener::bind(address));
    co_return Ok(fromQsox(std::move(listener)));
}

Future<NetResult<std::pair<arc::TcpStream, qsox::SocketAddress>>> TcpListener::accept() {
    auto res = co_await this->rioPoll([this](Context& cx, uint64_t& id) {
        return this->pollAccept(cx, id);
    });

    ARC_CO_UNWRAP_INTO(auto pair, std::move(res));
    auto socket = TcpStream::fromQsox(std::move(pair.first));

    co_return Ok(std::make_pair(std::move(socket), std::move(pair.second)));
}

std::optional<TcpListener::PollAcceptResult> TcpListener::pollAccept(Context& cx, uint64_t& id) {
    while (true) {
        auto ready = m_io.pollReady(Interest::Readable | Interest::Error, cx, id);
        if ((ready & Interest::Readable) == 0) {
            return std::nullopt;
        }

        auto res = m_listener.accept();

        if (res.isOk()) {
            return Ok(std::move(res).unwrap());
        }

        auto err = res.unwrapErr();
        if (err == qsox::Error::WouldBlock) {
            m_io.clearReadiness(Interest::Readable);
        } else {
            return Err(err);
        }
    }
}

NetResult<qsox::SocketAddress> TcpListener::localAddress() const {
    return m_listener.localAddress();
}

}