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

IocpPipeContext::~IocpPipeContext() {
    if (m_pipe) CloseHandle(m_pipe);
}

void IocpHandleContext::setCallback(void* data, Callback cb) {
    auto _lock = m_lock.lock();
    this->setCallbackLocked(data, cb);
}

void IocpHandleContext::setCallbackLocked(void* data, Callback cb) {
    m_data = data;
    m_callback = cb;
}

void IocpHandleContext::createEvent() {
    auto _lock = m_lock.lock();
    if (m_ov.hEvent) return;
    m_ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

void IocpHandleContext::notifySuccess(DWORD transferred) {
    auto _lock = m_lock.lock();
    if (m_callback) m_callback(m_data, this, transferred, 0);
}

void IocpHandleContext::notifyError(DWORD transferred, DWORD errorCode) {
    auto _lock = m_lock.lock();
    if (m_callback) m_callback(m_data, this, transferred, errorCode);
}

// IocpPipe Connection

IocpPipeConnectAwaiter::IocpPipeConnectAwaiter(WinHandle handle) {
    m_iocpContext = std::make_unique<IocpPipeContext>();
    m_iocpContext->m_pipe = handle;
    m_iocpContext->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpPipeConnectAwaiter*>(selfptr);
        if (errorCode == 0) {
            self->complete(Ok());
        } else {
            self->complete(Err(fmt::format("ConnectNamedPipe failed: {}", lastWinError(errorCode))));
        }
    });
}

IocpPipeConnectAwaiter::~IocpPipeConnectAwaiter() {}

std::optional<Result<IocpPipe>> IocpPipeConnectAwaiter::poll(Context& cx) {
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    // register with the iocp driver
    auto rt = Runtime::current();
    ARC_DEBUG_ASSERT(rt);

    m_waker = cx.cloneWaker();
    auto pipe = m_iocpContext->m_pipe;

    auto& iocpDriver = rt->iocpDriver();
    GEODE_UNWRAP(iocpDriver.registerIo(pipe, m_iocpContext.get(), HandleType::Pipe));

    // try to connect
    m_iocpContext->createEvent();

    if (ConnectNamedPipe(pipe, m_iocpContext->overlapped())) {
        return Ok(this->intoPipe());
    }

    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
        // already connected
        return Ok(this->intoPipe());
    }

    if (err != ERROR_IO_PENDING) {
        // actual error, fail
        return Err(fmt::format("ConnectNamedPipe failed: {}", lastWinError(err)));
    }

    // pending, wait for a notification from iocp driver
    return std::nullopt;
}

void IocpPipeConnectAwaiter::complete(Result<> result) {
    m_iocpContext->setCallbackLocked(nullptr, nullptr);

    if (result) {
        m_result = Ok(this->intoPipe());
    } else {
        m_result = Err(result.unwrapErr());
    }

    if (m_waker) {
        m_waker->wake();
        m_waker.reset();
    }
}

IocpPipe IocpPipeConnectAwaiter::intoPipe() {
    return IocpPipe{std::move(m_iocpContext)};
}

// IocpPipe writes

IocpPipeWriteAwaiter::IocpPipeWriteAwaiter(IocpPipeContext* context, const void* buffer, size_t length)
    : m_context(context), m_buffer(buffer), m_length(length)
{
    m_context->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpPipeWriteAwaiter*>(selfptr);
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

IocpPipeWriteAwaiter::~IocpPipeWriteAwaiter() {
    m_context->setCallback(nullptr, nullptr);
    CancelIoEx(m_context->m_pipe, m_context->overlapped());
}

std::optional<Result<size_t>> IocpPipeWriteAwaiter::poll(Context& cx) {
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    m_waker = cx.cloneWaker();

    DWORD writtenBytes = 0;
    if (WriteFile(m_context->m_pipe, m_buffer, m_length, &writtenBytes, m_context->overlapped())) {
        return Ok(writtenBytes);
    }

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        // actual error, fail
        return Err(fmt::format("WriteFile failed: {}", lastWinError(err)));
    }

    // pending, wait for a notification from iocp driver
    return std::nullopt;
}

// IocpPipe reads

IocpPipeReadAwaiter::IocpPipeReadAwaiter(IocpPipeContext* context, void* buffer, size_t length)
    : m_context(context), m_buffer(buffer), m_length(length)
{
    m_context->setCallback(this, [](void* selfptr, IocpHandleContext* ctx, DWORD bytesTransferred, DWORD errorCode) {
        auto* self = static_cast<IocpPipeReadAwaiter*>(selfptr);
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

IocpPipeReadAwaiter::~IocpPipeReadAwaiter() {
    m_context->setCallback(nullptr, nullptr);
    CancelIoEx(m_context->m_pipe, m_context->overlapped());
}

std::optional<Result<size_t>> IocpPipeReadAwaiter::poll(Context& cx) {
    if (m_result) return std::move(*m_result);

    // if already registered, do nothing
    if (m_waker) return std::nullopt;

    m_waker = cx.cloneWaker();

    DWORD readBytes = 0;
    if (ReadFile(m_context->m_pipe, m_buffer, m_length, &readBytes, m_context->overlapped())) {
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

// IocpPipe itself

IocpPipeConnectAwaiter IocpPipe::listen(WinHandle handle) {
    return IocpPipeConnectAwaiter{handle};
}

IocpPipeReadAwaiter IocpPipe::read(void* buffer, size_t length) {
    return IocpPipeReadAwaiter{m_iocpContext.get(), buffer, length};
}

IocpPipeWriteAwaiter IocpPipe::write(const void* buffer, size_t length) {
    return IocpPipeWriteAwaiter{m_iocpContext.get(), buffer, length};
}

IocpPipe::~IocpPipe() {}

IocpPipe::IocpPipe(std::unique_ptr<IocpPipeContext> context) : m_iocpContext(std::move(context)) {}

}
