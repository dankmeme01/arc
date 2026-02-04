#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_IOCP
ARC_FATAL_NO_FEATURE(iocp)
#else

#include <arc/future/Pollable.hpp>
#include <arc/runtime/IocpDriver.hpp>
#include "IocpMisc.hpp"

namespace arc {

struct IocpPipeConnectAwaiter;
struct IocpPipeReadAwaiter;
struct IocpPipeWriteAwaiter;

struct IocpPipeContext : public IocpHandleContext {
    WinHandle m_pipe;

    ~IocpPipeContext();
};

class IocpPipe {
public:
    /// Creates a new IOCP pipe from the given handle and waits for a connection
    /// This takes ownership of the handle, regardless of whether the connection is successful or not.
    static IocpPipeConnectAwaiter listen(WinHandle handle);

    /// Read from the pipe asynchronously
    IocpPipeReadAwaiter read(void* buffer, size_t length);
    /// Write to the pipe asynchronously
    IocpPipeWriteAwaiter write(const void* buffer, size_t length);

    ~IocpPipe();

    IocpPipe(const IocpPipe&) = delete;
    IocpPipe& operator=(const IocpPipe&) = delete;
    IocpPipe(IocpPipe&& other) noexcept = default;
    IocpPipe& operator=(IocpPipe&& other) = default;

    WinHandle handle() const { return m_iocpContext->m_pipe; }

private:
    friend struct IocpPipeConnectAwaiter;

    std::unique_ptr<IocpPipeContext> m_iocpContext;

    IocpPipe(std::unique_ptr<IocpPipeContext> context);
};

struct IocpPipeConnectAwaiter : Pollable<IocpPipeConnectAwaiter, Result<IocpPipe>> {
    explicit IocpPipeConnectAwaiter(WinHandle handle);
    ~IocpPipeConnectAwaiter();

    std::optional<Result<IocpPipe>> poll(Context& cx);

private:
    std::unique_ptr<IocpPipeContext> m_iocpContext;
    std::optional<Waker> m_waker;
    std::optional<Result<IocpPipe>> m_result;

    void complete(Result<> result);
    IocpPipe intoPipe();
};

struct IocpPipeReadAwaiter : Pollable<IocpPipeReadAwaiter, Result<size_t>> {
    IocpPipeReadAwaiter(IocpPipeContext* context, void* buffer, size_t length);
    ~IocpPipeReadAwaiter();

    std::optional<Result<size_t>> poll(Context& cx);

private:
    IocpPipeContext* m_context;
    void* m_buffer;
    size_t m_length;
    std::optional<Waker> m_waker;
    std::optional<Result<size_t>> m_result;
};

struct IocpPipeWriteAwaiter : Pollable<IocpPipeWriteAwaiter, Result<size_t>> {
    IocpPipeWriteAwaiter(IocpPipeContext* context, const void* buffer, size_t length);
    ~IocpPipeWriteAwaiter();

    std::optional<Result<size_t>> poll(Context& cx);
private:
    IocpPipeContext* m_context;
    const void* m_buffer;
    size_t m_length;
    std::optional<Waker> m_waker;
    std::optional<Result<size_t>> m_result;
};

}

#endif
