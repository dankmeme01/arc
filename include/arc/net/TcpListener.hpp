#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_NET
ARC_FATAL_NO_FEATURE(net)
#else

#include "EventIoBase.hpp"
#include "TcpStream.hpp"

#include <qsox/TcpListener.hpp>

namespace arc {

class TcpListener : public EventIoBase<TcpListener> {
public:
    using AcceptResult = NetResult<std::pair<arc::TcpStream, qsox::SocketAddress>>;
    using PollAcceptResult = NetResult<std::pair<qsox::TcpStream, qsox::SocketAddress>>;

    ~TcpListener();

    // Creates a new TCP listener, binding to the given address
    static Future<NetResult<TcpListener>> bind(const qsox::SocketAddress& address);

    // Creates a new TCP listener, binding to the given address
    // Convenience shorthand, address must be in format ip:port
    static Future<NetResult<TcpListener>> bind(std::string_view address);

    TcpListener(TcpListener&& other) noexcept = default;
    TcpListener& operator=(TcpListener&& other) noexcept = default;

    // Accepts a new incoming connection, blocking until one is available.
    Future<AcceptResult> accept();

    NetResult<qsox::SocketAddress> localAddress() const;

    /// Get the handle to the inner `qsox::TcpListener`.
    inline qsox::TcpListener& inner() noexcept {
        return m_listener;
    }

private:
    qsox::TcpListener m_listener;

    TcpListener(qsox::TcpListener listener, Registration io) : EventIoBase(std::move(io)), m_listener(std::move(listener)) {}

    static TcpListener fromQsox(qsox::TcpListener listener);
    std::optional<PollAcceptResult> pollAccept(Context& cx, uint64_t& id);

};

}

#endif
