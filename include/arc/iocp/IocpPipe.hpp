#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_IOCP
ARC_FATAL_NO_FEATURE(iocp)
#else

#include <arc/future/Pollable.hpp>
#include <arc/runtime/IocpDriver.hpp>
#include "IocpMisc.hpp"

namespace arc {

struct IocpPipeListenAwaiter;

struct IocpPipeContext : public IocpHandleContext {
    IocpPipeContext(HANDLE pipe) {
        m_handle = pipe;
    }
};

class IocpPipe {
public:
    /// Creates a new IOCP pipe from the given handle and waits for a connection
    /// This takes ownership of the handle, regardless of whether the connection is successful or not.
    static IocpPipeListenAwaiter listen(WinHandle handle);
    /// Opens the named pipe with the given name, errors if it does not exist
    static Result<IocpPipe> open(const std::string& name);
    /// Opens the named pipe with the given name, errors if it does not exist
    static Result<IocpPipe> open(const std::wstring& name);
    /// Adopt the given handle to a named pipe, allowing async reads and writes
    static Result<IocpPipe> open(HANDLE handle);

    /// Read from the pipe asynchronously
    IocpReadAwaiter read(void* buffer, size_t length);
    /// Write to the pipe asynchronously
    IocpWriteAwaiter write(const void* buffer, size_t length);

    ~IocpPipe();

    IocpPipe(const IocpPipe&) = delete;
    IocpPipe& operator=(const IocpPipe&) = delete;
    IocpPipe(IocpPipe&& other) noexcept = default;
    IocpPipe& operator=(IocpPipe&& other) = default;

    WinHandle handle() const { return m_iocpContext->handle(); }

private:
    friend struct IocpPipeListenAwaiter;

    std::unique_ptr<IocpPipeContext> m_iocpContext;

    IocpPipe(std::unique_ptr<IocpPipeContext> context);
};

struct IocpPipeListenAwaiter : Pollable<IocpPipeListenAwaiter, Result<IocpPipe>> {
    explicit IocpPipeListenAwaiter(WinHandle handle);
    ~IocpPipeListenAwaiter();

    std::optional<Result<IocpPipe>> poll(Context& cx);

private:
    std::unique_ptr<IocpPipeContext> m_iocpContext;
    IocpOpenAwaiter m_inner;

    void complete(Result<> result);
    IocpPipe intoPipe();
};

}

#endif
