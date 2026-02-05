#include <arc/iocp/IocpPipe.hpp>
#include <arc/runtime/Runtime.hpp>

namespace arc {

std::string lastWinError(DWORD code) {
    char errorBuf[512]; // enough for most messages

    auto result = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), errorBuf, sizeof(errorBuf), nullptr);

    if (result == 0) {
        return fmt::format("Unknown ({})", code);
    } else {
        auto msg = std::string(errorBuf, errorBuf + result);

        // the string sometimes includes a crlf, strip it, also remove unprintable chars
        msg.erase(std::find_if(msg.rbegin(), msg.rend(), [](unsigned char ch) {
            return ch != '\r' && ch != '\n' && ch < 127;
        }).base(), msg.end());

        return msg;
    }
}

IocpHandleContext::~IocpHandleContext() {
    if (m_handle) CloseHandle(m_handle);
}

void IocpHandleContext::setCallback(void* data, Callback cb) {
    auto _lock = m_lock.lock();
    this->setCallbackLocked(data, cb);
}

void IocpHandleContext::setCallbackLocked(void* data, Callback cb) {
    m_data = data;
    m_callback = cb;
}

void IocpHandleContext::notifySuccess(DWORD transferred) {
    auto _lock = m_lock.lock();
    if (m_callback) m_callback(m_data, this, transferred, 0);
}

void IocpHandleContext::notifyError(DWORD transferred, DWORD errorCode) {
    auto _lock = m_lock.lock();
    if (m_callback) m_callback(m_data, this, transferred, errorCode);
}

// Read / Write awaiters

IocpReadAwaiter::IocpReadAwaiter(IocpHandleContext* context, void* buffer, size_t length)
    : m_context(context), m_buffer(buffer), m_length(length)
{
    m_context->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpReadAwaiter*>(selfptr);
        self->m_context->setCallbackLocked(nullptr, nullptr);

        if (errorCode == 0) {
            self->m_result = Ok(static_cast<size_t>(bytesTransferred));
        } else {
            self->m_result = Err(fmt::format("ReadFile failed: {}", lastWinError(errorCode)));
        }

        if (self->m_waker) {
            self->m_waker->wake();
            self->m_waker.reset();
        }
    });
}

IocpReadAwaiter::~IocpReadAwaiter() {
    m_context->setCallback(nullptr, nullptr);
    if (m_waker) CancelIoEx(m_context->handle(), m_context->overlapped());
}

std::optional<Result<size_t>> IocpReadAwaiter::poll(Context& cx) {
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    m_waker = cx.cloneWaker();

    DWORD readBytes = 0;
    if (ReadFile(m_context->handle(), m_buffer, m_length, &readBytes, m_context->overlapped())) {
        return Ok(readBytes);
    }

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        // actual error, fail
        return Err(fmt::format("ReadFile failed: {}", lastWinError(err)));
    }

    // pending, wait for a notification from iocp driver
    return std::nullopt;
}

IocpWriteAwaiter::IocpWriteAwaiter(IocpHandleContext* context, const void* buffer, size_t length)
    : m_context(context), m_buffer(buffer), m_length(length)
{
    m_context->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpWriteAwaiter*>(selfptr);
        self->m_context->setCallbackLocked(nullptr, nullptr);

        if (errorCode == 0) {
            self->m_result = Ok(static_cast<size_t>(bytesTransferred));
        } else {
            self->m_result = Err(fmt::format("WriteFile failed: {}", lastWinError(errorCode)));
        }

        if (self->m_waker) {
            self->m_waker->wake();
            self->m_waker.reset();
        }
    });
}

IocpWriteAwaiter::~IocpWriteAwaiter() {
    m_context->setCallback(nullptr, nullptr);
    if (m_waker) CancelIoEx(m_context->handle(), m_context->overlapped());
}

std::optional<Result<size_t>> IocpWriteAwaiter::poll(Context& cx) {
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    m_waker = cx.cloneWaker();

    if (WriteFile(m_context->handle(), m_buffer, m_length, nullptr, m_context->overlapped())) {
        DWORD transferred = 0;
        if (GetOverlappedResult(m_context->handle(), m_context->overlapped(), &transferred, FALSE)) {
            printWarn("Overlapped ok: {}", transferred);
            return Ok(transferred);
        }
    }

    DWORD err = GetLastError();

    if (err != ERROR_IO_PENDING) {
        // actual error, fail
        return Err(fmt::format("WriteFile failed: {}", lastWinError(err)));
    }

    // pending, wait for a notification from iocp driver
    return std::nullopt;
}

IocpOpenAwaiter::IocpOpenAwaiter(IocpHandleContext* context, OpenFn fn) : m_context(context), m_openFn(fn) {
    m_context->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpOpenAwaiter*>(selfptr);
        self->m_context->setCallbackLocked(nullptr, nullptr);

        if (errorCode == 0) {
            self->m_result = Ok();
        } else {
            self->m_result = Err(fmt::format("IOCP open failed: {}", lastWinError(errorCode)));
        }

        if (self->m_waker) {
            self->m_waker->wake();
            self->m_waker.reset();
        }
    });
}

IocpOpenAwaiter::~IocpOpenAwaiter() {}

std::optional<Result<>> IocpOpenAwaiter::poll(Context& cx) {
    // if already finished, return result
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    // register with the iocp driver
    auto rt = Runtime::current();
    ARC_DEBUG_ASSERT(rt);

    m_waker = cx.cloneWaker();
    auto handle = m_context->handle();

    auto& iocpDriver = rt->iocpDriver();
    GEODE_UNWRAP(iocpDriver.registerIo(handle, m_context, HandleType::Pipe));

    // try to connect

    if (m_openFn(m_context)) {
        return Ok();
    }

    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
        // already connected
        return Ok();
    }

    if (err != ERROR_IO_PENDING) {
        // actual error, fail
        return Err(fmt::format("ConnectNamedPipe failed: {}", lastWinError(err)));
    }

    // pending, wait for a notification from iocp driver
    return std::nullopt;
}

// IocpPipe Connection

IocpPipeListenAwaiter::IocpPipeListenAwaiter(WinHandle handle)
    : m_iocpContext(std::make_unique<IocpPipeContext>(handle)), m_inner(m_iocpContext.get(), [](IocpHandleContext* ctx) -> bool {
        if (ConnectNamedPipe(ctx->handle(), ctx->overlapped())) {
            return true;
        }

        auto err = GetLastError();
        return err == ERROR_PIPE_CONNECTED;
    })
{}

IocpPipeListenAwaiter::~IocpPipeListenAwaiter() {}

std::optional<Result<IocpPipe>> IocpPipeListenAwaiter::poll(Context& cx) {
    if (auto res = m_inner.poll(cx)) {
        if (res->isOk()) {
            return Ok(this->intoPipe());
        }
        return Err(res->unwrapErr());
    }
    return std::nullopt;
}

IocpPipe IocpPipeListenAwaiter::intoPipe() {
    return IocpPipe{std::move(m_iocpContext)};
}

// IocpPipe itself

IocpPipeListenAwaiter IocpPipe::listen(WinHandle handle) {
    return IocpPipeListenAwaiter{handle};
}

Result<IocpPipe> IocpPipe::open(const std::string& name) {
    auto handle = CreateFileA(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        return Err("failed to open named pipe");
    }

    return open(handle);
}

Result<IocpPipe> IocpPipe::open(const std::wstring& name) {
    auto handle = CreateFileW(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        return Err("failed to open named pipe");
    }

    return open(handle);
}

Result<IocpPipe> IocpPipe::open(HANDLE handle) {
    // I got stuck here for a while, but apparently you need to set this flag
    // so that you don't receive IOCP packets when transfers complete synchronously
    SetFileCompletionNotificationModes(
        handle,
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
    );

    // register with the iocp driver
    auto rt = Runtime::current();
    ARC_DEBUG_ASSERT(rt);

    auto context = std::make_unique<IocpPipeContext>(handle);
    auto& iocpDriver = rt->iocpDriver();
    GEODE_UNWRAP(iocpDriver.registerIo(handle, context.get(), HandleType::Pipe));

    return Ok(IocpPipe { std::move(context) });
}

IocpReadAwaiter IocpPipe::read(void* buffer, size_t length) {
    return IocpReadAwaiter{m_iocpContext.get(), buffer, length};
}

IocpWriteAwaiter IocpPipe::write(const void* buffer, size_t length) {
    return IocpWriteAwaiter{m_iocpContext.get(), buffer, length};
}

IocpPipe::~IocpPipe() {}

IocpPipe::IocpPipe(std::unique_ptr<IocpPipeContext> context) : m_iocpContext(std::move(context)) {}

}
