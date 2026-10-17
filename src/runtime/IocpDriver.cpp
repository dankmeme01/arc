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
        .m_returnRetired = &IocpDriver::vReturnRetired,
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

void IocpDriver::returnRetired(IocpHandleContext* context) {
    m_vtable->m_returnRetired(this, context);
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

        ARC_TRACE("[IocpDriver] completed IO {}, {} bytes, overlapped at {}", ctx->handle(), bytes, (void*)ov);
        ctx->notifySuccess(bytes);
    }

    {
        // check the retired handles
        auto handles = m_retired.lock();
        for (size_t i = 0; i < handles->size();) {
            auto& ctx = (*handles)[i];
            if (HasOverlappedIoCompleted(ctx->overlapped())) {
                ARC_TRACE("[IocpDriver] destroying retired handle {}", ctx->handle());
                handles->erase(handles->begin() + i);
            } else {
                i++;
            }
        }
    }
}

Result<> IocpDriver::vRegisterIo(IocpDriver* self, WinHandle handle, IocpHandleContext* ctx, HandleType type) {
    if (!handle) {
        return Err("invalid handle");
    }

    auto result = CreateIoCompletionPort(handle, self->m_iocp, (ULONG_PTR)ctx, 0);
    ARC_TRACE("[IocpDriver] Registered handle {}", handle);

    if (result != self->m_iocp) {
        return Err(fmt::format("failed to associate handle with IOCP: {}", lastWinError()));
    }

    return Ok();
}

void IocpDriver::vReturnRetired(IocpDriver* self, IocpHandleContext* ctx) {
    self->m_retired.lock()->emplace_back(ctx);
}

}