#pragma once
#include "EventIoBase.hpp"

#include <qsox/UdpSocket.hpp>

namespace arc {

class UdpSocket : public EventIoBase<UdpSocket> {
public:
    ~UdpSocket();

    // Creates a new UDP socket, binding to the given address
    static Future<NetResult<UdpSocket>> bind(const qsox::SocketAddress& address);
    // Creates a new UDP socket, binding to 0.0.0.0 and a random port
    static Future<NetResult<UdpSocket>> bindAny(bool ipv6 = true);

    UdpSocket(UdpSocket&& other) noexcept = default;
    UdpSocket& operator=(UdpSocket&& other) noexcept = default;

    // Connects the UDP socket to a remote address
    // This sets the default destination for send operations, and limits the packets received with 'recv()' to that address.
    NetResult<void> connect(const qsox::SocketAddress& address);

    // Disconnects the UDP socket from the remote address
    NetResult<void> disconnect();

    // Sends a datagram to the specified address. Returns the number of bytes sent.
    Future<NetResult<size_t>> sendTo(const void* buffer, size_t size, const qsox::SocketAddress& destination);

    // Sends a datagram to the connected address. Returns the number of bytes sent.
    // Will fail if the socket is not connected.
    Future<NetResult<size_t>> send(const void* buffer, size_t size);

    // Receives a single datagram from the socket. If the buffer is too small, excess data is discarded.
    // On success, returns the number of bytes received.
    Future<NetResult<size_t>> recvFrom(void* buffer, size_t size, qsox::SocketAddress& sender);

    // Receives a single datagram from the connected address. If the buffer is too small, excess data is discarded.
    // On success returns the number of bytes received, will fail if the socket is not connected.
    Future<NetResult<size_t>> recv(void* buffer, size_t size);

    // Peeks at the next datagram in the socket without removing it from the queue.
    Future<NetResult<size_t>> peekFrom(void* buffer, size_t size, qsox::SocketAddress& sender);

    // Peeks at the next datagram in the connected socket without removing it from the queue.
    // Will fail if the socket is not connected.
    Future<NetResult<size_t>> peek(void* buffer, size_t size);

    NetResult<qsox::SocketAddress> localAddress() const;
    NetResult<qsox::SocketAddress> remoteAddress() const;

    /// Get the handle to the inner `qsox::UdpSocket`.
    inline qsox::UdpSocket& inner() noexcept {
        return m_socket;
    }

private:
    qsox::UdpSocket m_socket;

    UdpSocket(qsox::UdpSocket socket, Registration io) : EventIoBase(std::move(io)), m_socket(std::move(socket)) {}

    std::optional<NetResult<size_t>> pollWrite(Context& cx, const void* data, size_t size, std::optional<qsox::SocketAddress> address, uint64_t& id);
    std::optional<NetResult<size_t>> pollRead(Context& cx, void* buf, size_t size, qsox::SocketAddress* senderOut, bool peek, uint64_t& id);

    static UdpSocket fromQsox(qsox::UdpSocket socket);
};

}