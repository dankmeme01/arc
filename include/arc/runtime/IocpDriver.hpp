#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_IOCP
ARC_FATAL_NO_FEATURE(iocp)
#else

#include <arc/task/Waker.hpp>
#include <asp/sync/SpinLock.hpp>
#include <asp/sync/Mutex.hpp>
#include <asp/ptr/SharedPtr.hpp>
#include <arc/future/Context.hpp>
#include <arc/util/Result.hpp>

namespace arc {

using WinHandle = void*;

enum class HandleType : uint32_t {
    Unknown = 0,
    Pipe,
};

class IocpDriver;
struct IocpHandleContext;

struct IocpDriverVtable {
    using RegisterIoFn = Result<>(*)(IocpDriver*, WinHandle, IocpHandleContext*, HandleType);

    RegisterIoFn m_registerIo;
};

class IocpDriver {
public:
    IocpDriver(asp::WeakPtr<Runtime> runtime);
    IocpDriver(const IocpDriver&) = delete;
    IocpDriver& operator=(const IocpDriver&) = delete;
    ~IocpDriver();

    Result<> registerIo(WinHandle handle, IocpHandleContext* context, HandleType type);

private:
    friend class Runtime;

    const IocpDriverVtable* m_vtable;
    asp::WeakPtr<Runtime> m_runtime;
    WinHandle m_iocp = nullptr;

    void doWork();

    static Result<> vRegisterIo(IocpDriver* self, WinHandle handle, IocpHandleContext* ctx, HandleType type);
};

}

#endif
