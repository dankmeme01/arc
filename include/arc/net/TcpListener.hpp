#pragma once
#include "EventIoBase.hpp"
#include "TcpStream.hpp"

#include <qsox/TcpListener.hpp>

namespace arc {

class TcpListener : public EventIoBase<TcpListener> {
public:
    using AcceptResult = NetResult<std::pair<arc::TcpStream, qsox::SocketAddress>>;
    using PollAcceptResult = NetResult<std::pair<qsox::TcpStream, qsox::SocketAddress>>;

    // Creates a new TCP listener, binding to the given address
    static Future<NetResult<TcpListener>> bind(const qsox::SocketAddress& address);

    TcpListener(TcpListener&& other) noexcept = default;
    TcpListener& operator=(TcpListener&& other) noexcept = default;

    // Accepts a new incoming connection, blocking until one is available.
    Future<AcceptResult> accept();

    NetResult<qsox::SocketAddress> localAddress() const;

private:
    qsox::TcpListener m_listener;

    TcpListener(qsox::TcpListener listener, Registration io) : EventIoBase(std::move(io)), m_listener(std::move(listener)) {}

    static TcpListener fromQsox(qsox::TcpListener listener);
    std::optional<PollAcceptResult> pollAccept(uint64_t& id);

};

}