#pragma once
#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/future/Pollable.hpp>
#include <arc/util/Trace.hpp>
#include <arc/util/Result.hpp>

#include <std23/function_ref.h>

namespace arc {

struct LogOnDtor {
    LogOnDtor(std::string_view name) : m_name(name) {}
    ~LogOnDtor() { trace("{}", m_name); }
private:
    std::string_view m_name;
};

template <typename T = void>
using NetResult = qsox::NetResult<T>;

qsox::Error errorFromSocket(SockFd fd);

template <typename Derived>
class EventIoBase {
public:
    EventIoBase(Registration io) : m_io(std::move(io)) {}

    EventIoBase(EventIoBase&& other) noexcept = default;
    EventIoBase& operator=(EventIoBase&& other) noexcept = default;

    ~EventIoBase() {
        this->unregister();
    }

    Future<NetResult<void>> pollReadable() {
        return this->pollReady(Interest::Readable | Interest::Error);
    }

    Future<NetResult<void>> pollWritable() {
        return this->pollReady(Interest::Writable | Interest::Error);
    }

    Future<NetResult<void>> pollReady(Interest interest) {
        uint64_t id = 0;
        Interest ready = co_await pollFunc([&] {
            return m_io.pollReady(interest, id);
        });
        if (id != 0) {
            m_io.unregister(id);
        }

        // check if there was an error
        if (ready & Interest::Error) {
            co_return Err(this->takeSocketError());
        }

        // otherwise, we are ready to return
        co_return Ok();
    }

    qsox::Error takeSocketError() {
        return errorFromSocket(m_io.rio->fd);
    }

protected:
    Registration m_io;

    using PollReadFn = std23::function_ref<NetResult<size_t>(void* buf, size_t size)>;
    using PollWriteFn = std23::function_ref<NetResult<size_t>(const void* buf, size_t size)>;

    void unregister() {
        if (!m_io.rio) return;
        auto rt = m_io.rio->runtime.lock();
        if (!rt || rt->isShuttingDown()) return;

        rt->ioDriver().unregisterIo(m_io.rio);
        m_io.rio.reset();
    }

    std::optional<qsox::Error> takeOrClearError() {
        auto err = this->takeSocketError();
        if (err == qsox::Error::Success) {
            m_io.clearReadiness(Interest::Error);
            return std::nullopt;
        } else {
            return err;
        }
    }

    std::optional<NetResult<size_t>> pollRead(uint64_t& id, void* buf, size_t size, PollReadFn readFn) {
        return this->pollCustom<size_t>(id, Interest::Readable, [&] -> NetResult<std::optional<size_t>> {
            auto res = readFn(buf, size);
            if (res.isOk()) {
                return Ok(res.unwrap());
            }

            auto err = res.unwrapErr();
            if (err == qsox::Error::WouldBlock) {
                m_io.clearReadiness(Interest::Readable);
                return Ok(std::nullopt);
            } else {
                return Err(err);
            }
        });
    }

    std::optional<NetResult<size_t>> pollWrite(uint64_t& id, const void* buf, size_t size, PollWriteFn writeFn) {
        return this->pollCustom<size_t>(id, Interest::Writable, [&] -> NetResult<std::optional<size_t>> {
            auto res = writeFn(buf, size);

            if (res.isOk()) {
                auto n = res.unwrap();
                // if not on windows, if we wrote less bytes than requested, that means the socket buffer is full
#ifndef _WIN32
                if (n > 0 && n < size) {
                    m_io.clearReadiness(Interest::Writable);
                }
#endif
                return Ok(n);
            }

            auto err = res.unwrapErr();
            if (err == qsox::Error::WouldBlock) {
                m_io.clearReadiness(Interest::Readable);
                return Ok(std::nullopt);
            } else {
                return Err(err);
            }
        });
    }

    /// A version of pollRead/pollWrite that allows you to more manually manage socket readiness.
    /// Does not clear readiness and simply passes on the result of the invoked function when ready.
    /// The function must return a NetResult<optional<T>>, if the inner value is non null, then the function immediately returns.
    /// Otherwise it loops, then calls pollReady and your function again.
    template <typename T = std::monostate>
    std::optional<NetResult<T>> pollCustom(uint64_t& id, Interest interest, auto fn) {
        while (true) {
            auto ready = m_io.pollReady(interest | Interest::Error, id);
            if (ready == 0) {
                return std::nullopt;
            } else if (ready & Interest::Error) {
                if (auto err = this->takeOrClearError()) {
                    return Err(*err);
                } else {
                    continue;
                }
            }

            auto res = fn();
            if (res.isErr()) {
                return Err(std::move(res).unwrapErr());
            }

            auto opt = std::move(res).unwrap();
            if (opt.has_value()) {
                return Ok(std::move(*opt));
            }
        }
    }

    auto rioPoll(auto fn) -> Future<typename ExtractOptional<std::invoke_result_t<decltype(fn), uint64_t&>>::type> {
        uint64_t id = 0;

        auto result = co_await pollFunc([&id, fn = std::move(fn)] mutable {
            return fn(id);
        });
        if (id != 0) {
            m_io.unregister(id);
        }
        co_return result;
    }
};

}