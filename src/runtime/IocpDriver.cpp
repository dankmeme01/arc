#include <arc/runtime/IocpDriver.hpp>
#include <arc/iocp/IocpMisc.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <Windows.h>

namespace arc {

IocpDriver::IocpDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    ARC_ASSERT(m_iocp != nullptr, "failed to create IOCP");

    static const IocpDriverVtable vtable {
        .m_registerIo = &IocpDriver::vRegisterIo,
    };

    m_vtable = &vtable;
}

IocpDriver::~IocpDriver() {
    if (m_iocp) {
        CloseHandle(m_iocp);
    }
}

Result<> IocpDriver::registerIo(WinHandle handle, IocpHandleContext* ctx, HandleType type) {
    return m_vtable->m_registerIo(this, handle, ctx, type);
}

void IocpDriver::doWork() {
    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED ov;

    while (true) {
        BOOL success = GetQueuedCompletionStatus(m_iocp, &bytes, &key, &ov, 0);
        auto* ctx = reinterpret_cast<IocpHandleContext*>(key);

        if (!success) {
            if (!ov) {
                // timed out
                break;
            }

            auto err = GetLastError();

            if (ctx) {
                ctx->notifyError(bytes, err);
            } else {
                printWarn("IocpDriver: GetQueuedCompletionStatus failed with error code {}", err);
                break;
            }
        }

        if (ctx) {
            ctx->notifySuccess(bytes);
        }
    }
}

Result<> IocpDriver::vRegisterIo(IocpDriver* self, WinHandle handle, IocpHandleContext* ctx, HandleType type) {
    if (!handle) {
        return Err("invalid handle");
    }

    auto result = CreateIoCompletionPort(handle, self->m_iocp, (ULONG_PTR)ctx, 0);
    if (result != self->m_iocp) {
        return Err(fmt::format("failed to associate handle with IOCP: {}", lastWinError()));
    }

    return Ok();
}

}