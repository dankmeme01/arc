#include <arc/net/UdpSocket.hpp>
#include <arc/util/Result.hpp>

using namespace qsox;

namespace arc {

UdpSocket UdpSocket::fromQsox(qsox::UdpSocket socket) {
    (void) socket.setNonBlocking(true);
    auto rio = ctx().runtime()->ioDriver().registerIo(socket.handle(), Interest::ReadWrite);
    return UdpSocket{std::move(socket), std::move(rio)};
}

Future<NetResult<UdpSocket>> UdpSocket::bind(const SocketAddress& address) {
    ARC_CO_UNWRAP_INTO(auto socket, qsox::UdpSocket::bind(address));
    co_return Ok(fromQsox(std::move(socket)));
}

Future<NetResult<UdpSocket>> UdpSocket::bindAny(bool ipv6) {
    ARC_CO_UNWRAP_INTO(auto socket, qsox::UdpSocket::bindAny(ipv6));
    co_return Ok(fromQsox(std::move(socket)));
}

NetResult<void> UdpSocket::connect(const SocketAddress& address) {
    return m_socket.connect(address);
}

NetResult<void> UdpSocket::disconnect() {
    return m_socket.disconnect();
}

Future<NetResult<size_t>> UdpSocket::sendTo(const void* buffer, size_t size, const SocketAddress& destination) {
    return this->rioPoll([this, buffer, size, dest = std::optional{destination}](uint64_t& id) {
        return this->pollWrite(buffer, size, dest, id);
    });
}

Future<NetResult<size_t>> UdpSocket::send(const void* buffer, size_t size) {
    return this->rioPoll([this, buffer, size](uint64_t& id) {
        return this->pollWrite(buffer, size, std::nullopt, id);
    });
}

Future<NetResult<size_t>> UdpSocket::recvFrom(void* buffer, size_t size, SocketAddress& sender) {
    return this->rioPoll([this, buffer, size, sender = &sender](uint64_t& id) {
        return this->pollRead(buffer, size, sender, false, id);
    });
}

Future<NetResult<size_t>> UdpSocket::recv(void* buffer, size_t size) {
    return this->rioPoll([this, buffer, size](uint64_t& id) {
        return this->pollRead(buffer, size, nullptr, false, id);
    });
}

Future<NetResult<size_t>> UdpSocket::peekFrom(void* buffer, size_t size, SocketAddress& sender) {
    return this->rioPoll([this, buffer, size, sender = &sender](uint64_t& id) {
        return this->pollRead(buffer, size, sender, true, id);
    });
}

Future<NetResult<size_t>> UdpSocket::peek(void* buffer, size_t size) {
    return this->rioPoll([this, buffer, size](uint64_t& id) {
        return this->pollRead(buffer, size, nullptr, true, id);
    });
}

NetResult<qsox::SocketAddress> UdpSocket::localAddress() const {
    return m_socket.localAddress();
}

NetResult<qsox::SocketAddress> UdpSocket::remoteAddress() const {
    return m_socket.remoteAddress();
}

std::optional<NetResult<size_t>> UdpSocket::pollWrite(const void* data, size_t size, std::optional<SocketAddress> address, uint64_t& id) {
    return EventIoBase::pollWrite(id, data, size, [&](auto buf, auto size) {
        if (address) {
            return m_socket.sendTo(buf, size, *address);
        } else {
            return m_socket.send(buf, size);
        }
    });
}

std::optional<NetResult<size_t>> UdpSocket::pollRead(void* buf, size_t size, SocketAddress* senderOut, bool peek, uint64_t& id) {
    return EventIoBase::pollRead(id, buf, size, [&](auto buf, auto size) {
        if (peek) {
            if (senderOut) {
                return m_socket.peekFrom(buf, size, *senderOut);
            } else {
                return m_socket.peek(buf, size);
            }
        } else {
            if (senderOut) {
                return m_socket.recvFrom(buf, size, *senderOut);
            } else {
                return m_socket.recv(buf, size);
            }
        }
    });
}

}