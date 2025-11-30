#include <arc/net/TcpStream.hpp>
#include <arc/task/Context.hpp>
#include <arc/util/Result.hpp>
#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>

using namespace qsox;

namespace arc {

TcpStream::SendAwaiter::~SendAwaiter() {
    if (m_id == 0) {
        m_stream->m_io.unregister(m_id);
    }
}

TcpStream::RecvAwaiter::~RecvAwaiter() {
    if (m_id == 0) {
        m_stream->m_io.unregister(m_id);
    }
}

arc::Future<NetResult<TcpStream>> TcpStream::connect(std::string_view address) {
    auto res = qsox::SocketAddress::parse(address);
    if (!res) {
        co_return Err(Error::InvalidArgument);
    }

    co_return co_await TcpStream::connect(*res);
}

arc::Future<NetResult<TcpStream>> TcpStream::connect(const qsox::SocketAddress& address) {
    ARC_CO_UNWRAP_INTO(auto stream, qsox::TcpStream::connectNonBlocking(address));
    auto rio = ctx().runtime()->ioDriver().registerIo(stream.handle(), Interest::ReadWrite);

    // wait until writable (connected)
    uint64_t id = 0;
    co_await pollFunc([&] {
        return rio.pollReady(Interest::Writable, id);
    });
    if (id != 0) {
        rio.unregister(id);
    }

    // optimistically create a tcpstream so that raii will unregister on error
    TcpStream out{std::move(stream), std::move(rio)};

    auto err = stream.getSocketError();
    if (err != Error::Success) {
        co_return Err(err);
    }

    co_return Ok(std::move(out));
}

std::optional<NetResult<size_t>> TcpStream::pollWrite(const void* data, size_t size, uint64_t& id) {
    while (true) {
        if (!m_io.pollReady(Interest::Writable, id)) {
            return std::nullopt;
        }

        auto res = m_stream.send(data, size);

        if (res.isOk()) {
            auto n = res.unwrap();
            // if not on windows, if we wrote less bytes than requested, that means the socket buffer is full
#ifndef _WIN32
            if (n > 0 && n < size) {
                m_io.clearReadiness(Interest::Writable);
            }
#endif
            return Ok(n);
        }

        auto err = res.unwrapErr();
        if (err == Error::WouldBlock) {
            m_io.clearReadiness(Interest::Writable);
        } else {
            return Err(err);
        }
    }
}

std::optional<NetResult<size_t>> TcpStream::pollRead(void* buf, size_t size, uint64_t& id) {
    while (true) {
        if (!m_io.pollReady(Interest::Readable, id)) {
            return std::nullopt;
        }

        auto res = m_stream.receive(buf, size);

        if (res.isOk()) {
            return Ok(res.unwrap());
        }

        auto err = res.unwrapErr();
        if (err == Error::WouldBlock) {
            m_io.clearReadiness(Interest::Readable);
        } else {
            return Err(err);
        }
    }
}

TcpStream::SendAwaiter TcpStream::send(const void* data, size_t size) {
    return SendAwaiter{this, data, size};
}

TcpStream::RecvAwaiter TcpStream::receive(void* buf, size_t size) {
    return RecvAwaiter{this, buf, size};
}

}