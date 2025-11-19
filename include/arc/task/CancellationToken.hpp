#pragma once

#include <atomic>

namespace arc {

struct CancellationToken {
    std::atomic<bool> m_cancelled{false};

    bool isCancelled() const noexcept {
        return m_cancelled.load(std::memory_order::acquire);
    }

    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order::release);
    }
};

}