#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_IOCP
ARC_FATAL_NO_FEATURE(iocp)
#else

#include <Windows.h>
#include <string>
#include <asp/sync/SpinLock.hpp>
#include <arc/future/Pollable.hpp>
#include <arc/task/Waker.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Result.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

std::string lastWinError(DWORD code = GetLastError());

struct IocpHandleContext {
    using Callback = void(*)(void*, IocpHandleContext*, DWORD bytesTransferred, DWORD errorCode);

    void setCallback(void* data, Callback cb);
    void setCallbackLocked(void* data, Callback cb);

    OVERLAPPED* overlapped() { return &m_ov; }
    HANDLE handle() const { return m_handle; }

    ~IocpHandleContext();

protected:
    friend class IocpDriver;
    OVERLAPPED m_ov{};
    HANDLE m_handle;
    asp::SpinLock<> m_lock;
    void* m_data = nullptr;
    Callback m_callback = nullptr;
    // -- fields above this line expected to be abi stable --

    void notifySuccess(DWORD transferred);
    void notifyError(DWORD transferred, DWORD errorCode);
};

struct IocpReadAwaiter : Pollable<IocpReadAwaiter, Result<size_t>> {
    IocpReadAwaiter(IocpHandleContext* context, void* buffer, size_t length);
    ~IocpReadAwaiter();

    std::optional<Result<size_t>> poll(Context& cx);

private:
    IocpHandleContext* m_context;
    void* m_buffer;
    size_t m_length;
    std::optional<Waker> m_waker;
    std::optional<Result<size_t>> m_result;
};

struct IocpWriteAwaiter : Pollable<IocpWriteAwaiter, Result<size_t>> {
    IocpWriteAwaiter(IocpHandleContext* context, const void* buffer, size_t length);
    ~IocpWriteAwaiter();

    std::optional<Result<size_t>> poll(Context& cx);
private:
    IocpHandleContext* m_context;
    const void* m_buffer;
    size_t m_length;
    std::optional<Waker> m_waker;
    std::optional<Result<size_t>> m_result;
};

struct IocpOpenAwaiter : Pollable<IocpOpenAwaiter, Result<>> {
    using OpenFn = bool(*)(IocpHandleContext*);

    IocpOpenAwaiter(IocpHandleContext* context, OpenFn fn);
    ~IocpOpenAwaiter();

    std::optional<Result<>> poll(Context& cx);

private:
    IocpHandleContext* m_context;
    OpenFn m_openFn;
    std::optional<Waker> m_waker;
    std::optional<Result<>> m_result;
};

}

#endif
