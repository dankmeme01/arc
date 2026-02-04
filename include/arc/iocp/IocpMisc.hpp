#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_IOCP
ARC_FATAL_NO_FEATURE(iocp)
#else

#include <Windows.h>
#include <string>
#include <asp/sync/SpinLock.hpp>

namespace arc {

std::string lastWinError(DWORD code = GetLastError());

struct IocpHandleContext {
    using Callback = void(*)(void*, IocpHandleContext*, DWORD bytesTransferred, DWORD errorCode);

    void createEvent();
    OVERLAPPED* overlapped() { return &m_ov; }
    void setCallback(void* data, Callback cb);
    void setCallbackLocked(void* data, Callback cb);

protected:
    friend class IocpDriver;
    OVERLAPPED m_ov{};
    asp::SpinLock<> m_lock;
    void* m_data = nullptr;
    Callback m_callback = nullptr;
    // -- fields above this line expected to be abi stable --

    void notifySuccess(DWORD transferred);
    void notifyError(DWORD transferred, DWORD errorCode);
};

}

#endif
