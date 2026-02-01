#include <arc/net/TcpStream.hpp>
#include <arc/util/Result.hpp>
#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace qsox;

namespace arc {

TcpStream::~TcpStream() {
    // deregister io before destroying the socket
    this->unregister();
}

TcpStream TcpStream::fromQsox(qsox::TcpStream socket) {
    (void) socket.setNonBlocking(true);
    auto rt = Runtime::current();
    ARC_ASSERT(rt, "No runtime available to register TcpStream");

    auto rio = rt->ioDriver().registerIo(socket.handle(), Interest::ReadWrite);
    return TcpStream{std::move(socket), std::move(rio)};
}

Future<NetResult<TcpStream>> TcpStream::connect(std::string_view address) {
    auto res = qsox::SocketAddress::parse(address);
    if (!res) {
        co_return Err(Error::InvalidArgument);
    }

    co_return co_await TcpStream::connect(*res);
}

Future<NetResult<TcpStream>> TcpStream::connect(SocketAddress address) {
    trace("(TCP) Connecting to {}", address.toString());

    // create a tcpstream immediately so that raii will unregister the io on error
    ARC_CO_UNWRAP_INTO(auto stream, qsox::TcpStream::connectNonBlocking(address));
    auto out = fromQsox(std::move(stream));

    // wait until writable (connected)
    ARC_CO_UNWRAP(co_await out.pollWritable());

    auto err = out.m_stream.getSocketError();
    if (err != Error::Success) {
        co_return Err(err);
    }

    co_return Ok(std::move(out));
}

Future<NetResult<void>> TcpStream::shutdown(ShutdownMode mode) {
    ARC_CO_UNWRAP(m_stream.shutdown(mode));
    co_return Ok();
}

NetResult<void> TcpStream::setNoDelay(bool noDelay) {
    return m_stream.setNoDelay(noDelay);
}

Future<NetResult<size_t>> TcpStream::send(const void* data, size_t size) {
    return this->rioPoll([this, data, size](Context& cx, uint64_t& id) {
        return this->pollWrite(cx, data, size, id);
    });
}

Future<NetResult<void>> TcpStream::sendAll(const void* datav, size_t size) {
    const char* data = reinterpret_cast<const char*>(datav);
    size_t remaining = size;

    uint64_t id = 0;

    NetResult<void> result = Ok();
    while (remaining > 0) {
        auto res = co_await pollFunc([&](Context& cx) {
            return this->pollWrite(cx, data, remaining, id);
        });

        if (!res) {
            result = Err(res.unwrapErr());
            break;
        }

        auto n = res.unwrap();
        data += n;
        remaining -= n;
    }

    if (id != 0) {
        m_io.unregister(id);
    }

    co_return result;
}

Future<NetResult<size_t>> TcpStream::receive(void* buffer, size_t size) {
    return this->rioPoll([this, buffer, size](Context& cx, uint64_t& id) {
        return this->pollRead(cx, buffer, size, id);
    });
}

Future<NetResult<void>> TcpStream::receiveExact(void* buffer, size_t size) {
    char* buf = reinterpret_cast<char*>(buffer);
    size_t remaining = size;

    uint64_t id = 0;

    NetResult<void> result = Ok();
    while (remaining > 0) {
        auto res = co_await pollFunc([&](Context& cx) {
            return this->pollRead(cx, buf, remaining, id);
        });

        if (!res) {
            result = Err(res.unwrapErr());
            break;
        }

        auto n = res.unwrap();
        buf += n;
        remaining -= n;
    }

    if (id != 0) {
        m_io.unregister(id);
    }

    co_return result;
}

Future<NetResult<size_t>> TcpStream::peek(void* buffer, size_t size) {
    return this->rioPoll([this, buffer, size](Context& cx, uint64_t& id) {
        return this->pollRead(cx, buffer, size, id, true);
    });
}

NetResult<qsox::SocketAddress> TcpStream::localAddress() const {
    return m_stream.localAddress();
}

NetResult<qsox::SocketAddress> TcpStream::remoteAddress() const {
    return m_stream.remoteAddress();
}

std::optional<NetResult<size_t>> TcpStream::pollWrite(Context& cx, const void* data, size_t size, uint64_t& id) {
    return EventIoBase::pollWrite(cx, id, data, size, [&](auto buf, auto size) {
        return m_stream.send(buf, size);
    });
}

std::optional<NetResult<size_t>> TcpStream::pollRead(Context& cx, void* buf, size_t size, uint64_t& id, bool peek) {
    return EventIoBase::pollRead(cx, id, buf, size, [&](auto buf, auto size) {
        return peek ? m_stream.peek(buf, size) : m_stream.receive(buf, size);
    });
}

}