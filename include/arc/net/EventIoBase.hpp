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

template <typename Derived>
class EventIoBase {
public:
    EventIoBase(Registration io) : m_io(std::move(io)) {}

    EventIoBase(EventIoBase&& other) noexcept = default;
    EventIoBase& operator=(EventIoBase&& other) noexcept = default;

    ~EventIoBase() {
        if (!m_io.rio) return;

        ctx().runtime()->ioDriver().unregisterIo(m_io.rio);
    }

    Future<NetResult<void>> pollReadable() {
        return this->pollReady(Interest::Readable);
    }

    Future<NetResult<void>> pollWritable() {
        return this->pollReady(Interest::Writable);
    }

    Future<NetResult<void>> pollReady(Interest interest) {
        uint64_t id = 0;
        co_await pollFunc([&] {
            return m_io.pollReady(interest, id);
        });
        if (id != 0) {
            m_io.unregister(id);
        }
        co_return Ok();
    }

protected:
    Registration m_io;

    using PollReadFn = std23::function_ref<NetResult<size_t>(void* buf, size_t size)>;
    using PollWriteFn = std23::function_ref<NetResult<size_t>(const void* buf, size_t size)>;

    std::optional<NetResult<size_t>> pollRead(uint64_t& id, void* buf, size_t size, PollReadFn readFn) {
        while (true) {
            if (!m_io.pollReady(Interest::Readable, id)) {
                return std::nullopt;
            }

            auto res = readFn(buf, size);

            if (res.isOk()) {
                return Ok(res.unwrap());
            }

            auto err = res.unwrapErr();
            if (err == qsox::Error::WouldBlock) {
                m_io.clearReadiness(Interest::Readable);
            } else {
                return Err(err);
            }
        }
    }

    std::optional<NetResult<size_t>> pollWrite(uint64_t& id, const void* buf, size_t size, PollWriteFn writeFn) {
        while (true) {
            if (!m_io.pollReady(Interest::Writable, id)) {
                return std::nullopt;
            }

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
                m_io.clearReadiness(Interest::Writable);
            } else {
                return Err(err);
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