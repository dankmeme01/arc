#pragma once
#include "EventIoBase.hpp"

#include <qsox/TcpStream.hpp>

namespace arc {

using qsox::ShutdownMode;

class TcpStream : public EventIoBase<TcpStream> {
public:
    // Creates a new TCP stream, connecting to the given address.
    static Future<NetResult<TcpStream>> connect(qsox::SocketAddress address);
    // Creates a new TCP stream, connecting to the given address.
    // Convenience shorthand, address must be in format ip:port
    static Future<NetResult<TcpStream>> connect(std::string_view address);

    TcpStream(TcpStream&& other) noexcept = default;
    TcpStream& operator=(TcpStream&& other) noexcept = default;

    // Shuts down the TCP stream for reading, writing, or both.
    Future<NetResult<void>> shutdown(ShutdownMode mode);

    // Sets the TCP_NODELAY option on the socket, which disables or enables the Nagle algorithm.
    // If `noDelay` is true, small packets are sent immediately without waiting for larger packets to accumulate.
    NetResult<void> setNoDelay(bool noDelay);

    // Sends data over this socket. Returns amount of bytes sent.
    Future<NetResult<size_t>> send(const void* data, size_t size);

    // Sends data over this socket, waiting until all data is sent, or an error occurs.
    Future<NetResult<void>> sendAll(const void* data, size_t size);

    // Receives data from the socket. Returns amount of bytes received.
    Future<NetResult<size_t>> receive(void* buffer, size_t size);

    // Receives data from the socket, waiting until the given buffer is full or an error occurs.
    Future<NetResult<void>> receiveExact(void* buffer, size_t size);

    // Peeks at incoming data without removing it from the queue.
    Future<NetResult<size_t>> peek(void* buffer, size_t size);

    NetResult<qsox::SocketAddress> localAddress() const;
    NetResult<qsox::SocketAddress> remoteAddress() const;

    /// Get the handle to the inner `qsox::TcpStream`.
    inline qsox::TcpStream& inner() noexcept {
        return m_stream;
    }

private:
    friend class TcpListener;

    qsox::TcpStream m_stream;

    TcpStream(qsox::TcpStream stream, Registration io) : EventIoBase(std::move(io)), m_stream(std::move(stream)) {}

    std::optional<NetResult<size_t>> pollWrite(const void* data, size_t size, uint64_t& id);
    std::optional<NetResult<size_t>> pollRead(void* buf, size_t size, uint64_t& id, bool peek = false);

    static TcpStream fromQsox(qsox::TcpStream socket);
};

}