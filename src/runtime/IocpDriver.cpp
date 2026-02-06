#include <arc/runtime/IocpDriver.hpp>
#include <arc/iocp/IocpMisc.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <Windows.h>

namespace arc {

IocpDriver::IocpDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    ARC_ASSERT(m_iocp != nullptr, "failed to create IOCP");

    static constexpr IocpDriverVtable vtable {
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
    std::array<OVERLAPPED_ENTRY, 64> entries;
    ULONG numEntries = 0;

    BOOL success = GetQueuedCompletionStatusEx(m_iocp, entries.data(), entries.size(), &numEntries, 0, FALSE);
    if (!success) {
        auto err = GetLastError();
        if (err != WAIT_TIMEOUT) {
            printWarn("IocpDriver: GetQueuedCompletionStatusEx failed with error code {}", err);
        }
        return;
    }

    for (auto i = 0u; i < numEntries; i++) {
        auto& entry = entries[i];
        DWORD bytes = entry.dwNumberOfBytesTransferred;
        OVERLAPPED* ov = entry.lpOverlapped;
        IocpHandleContext* ctx = reinterpret_cast<IocpHandleContext*>(entry.lpCompletionKey);

        // handle errors
        if (bytes == 0) {
            BOOL ok = GetOverlappedResult(ctx->handle(), ov, &bytes, FALSE);
            if (!ok) {
                auto err = GetLastError();
                printWarn("[IocpDriver] IO {} errored: {}", ctx->handle(), err);
                ctx->notifyError(bytes, err);
                continue;
            }
        }

        trace("[IocpDriver] completed IO {}, {} bytes, overlapped at {}", ctx->handle(), bytes, (void*)ov);
        ctx->notifySuccess(bytes);
    }
}

Result<> IocpDriver::vRegisterIo(IocpDriver* self, WinHandle handle, IocpHandleContext* ctx, HandleType type) {
    if (!handle) {
        return Err("invalid handle");
    }

    auto result = CreateIoCompletionPort(handle, self->m_iocp, (ULONG_PTR)ctx, 0);
    trace("[IocpDriver] Registered handle {}", handle);

    if (result != self->m_iocp) {
        return Err(fmt::format("failed to associate handle with IOCP: {}", lastWinError()));
    }

    return Ok();
}

}