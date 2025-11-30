#pragma once
#include "EventIoBase.hpp"
#include <arc/runtime/IoDriver.hpp>

#include <qsox/TcpStream.hpp>

namespace arc {

template <typename T>
using NetResult = qsox::NetResult<T>;

class TcpStream : public EventIoBase {
public:
    static arc::Future<NetResult<TcpStream>> connect(const qsox::SocketAddress& address);
    static arc::Future<NetResult<TcpStream>> connect(std::string_view address);

private:
    qsox::TcpStream m_stream;

    TcpStream(qsox::TcpStream stream, Registration io) : EventIoBase(std::move(io)), m_stream(std::move(stream)) {}

    std::optional<NetResult<size_t>> pollWrite(const void* data, size_t size, uint64_t& id);
    std::optional<NetResult<size_t>> pollRead(void* buf, size_t size, uint64_t& id);

public:
    struct [[nodiscard]] SendAwaiter : PollableBase<SendAwaiter, NetResult<size_t>> {
        SendAwaiter(TcpStream* stream, const void* data, size_t size)
            : m_stream(stream), m_data(data), m_size(size) {}

        ~SendAwaiter();

        std::optional<NetResult<size_t>> poll() {
            return m_stream->pollWrite(m_data, m_size, m_id);
        }
    private:
        TcpStream* m_stream;
        const void* m_data;
        size_t m_size;
        uint64_t m_id = 0;
    };

    struct [[nodiscard]] RecvAwaiter : PollableBase<RecvAwaiter, NetResult<size_t>> {
        RecvAwaiter(TcpStream* stream, void* buf, size_t size)
            : m_stream(stream), m_buf(buf), m_size(size) {}

        ~RecvAwaiter();

        std::optional<NetResult<size_t>> poll() {
            return m_stream->pollRead(m_buf, m_size, m_id);
        }
    private:
        TcpStream* m_stream;
        void* m_buf;
        size_t m_size;
        uint64_t m_id;
    };

    // Sends data over this socket. Returns amount of bytes sent.
    SendAwaiter send(const void* data, size_t size);

    // Receives data from the socket. Returns amount of bytes received.
    RecvAwaiter receive(void* buffer, size_t size);
};

}