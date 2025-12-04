#pragma once

#include <atomic>
#include <arc/future/Pollable.hpp>
#include <arc/sync/Notify.hpp>

namespace arc {

struct CancellationToken {
    std::atomic<bool> m_cancelled{false};
    arc::Notify m_notify;

    CancellationToken() = default;

    CancellationToken(CancellationToken&&) = delete;
    CancellationToken& operator=(CancellationToken&&) = delete;

    bool isCancelled() const noexcept {
        return m_cancelled.load(std::memory_order::acquire);
    }

    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order::release);
        m_notify.notifyAll();
    }

    struct Awaiter : PollableBase<Awaiter> {
        explicit Awaiter(CancellationToken* token) : m_token(token) {}
        Awaiter(Awaiter&&) noexcept = default;
        Awaiter& operator=(Awaiter&&) noexcept = default;
        ~Awaiter() = default;

        bool poll();

    private:
        CancellationToken* m_token;
        std::optional<Notified> m_notified;
    };

    Awaiter waitCancelled() noexcept {
        return Awaiter{this};
    }
};

}