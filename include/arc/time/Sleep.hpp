#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_TIME
ARC_FATAL_NO_FEATURE(time)
#else

#include <arc/future/Pollable.hpp>
#include <arc/runtime/Runtime.hpp>
#include <asp/time/Instant.hpp>
#include <cstdint>

namespace arc {

struct ARC_NODISCARD Sleep : Pollable<Sleep> {
    explicit Sleep(asp::time::Instant expiry) noexcept : m_expiry(expiry) {}
    ~Sleep();

    Sleep(Sleep&& other) noexcept;
    Sleep& operator=(Sleep&& other) noexcept;

    bool poll(Context& cx);

private:
    asp::time::Instant m_expiry;
    uint64_t m_id = 0;
    asp::WeakPtr<Runtime> m_runtime;
};

Sleep sleep(asp::time::Duration duration) noexcept;
Sleep sleepFor(asp::time::Duration duration) noexcept;
Sleep sleepUntil(asp::time::Instant expiry) noexcept;

}

#endif
