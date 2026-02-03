#pragma once

#include <atomic>
#include <arc/future/Pollable.hpp>
#include <arc/sync/Notify.hpp>

namespace arc {

struct CancellationToken {
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

    struct ARC_NODISCARD Awaiter : Pollable<Awaiter> {
        explicit Awaiter(CancellationToken* token) : m_token(token) {}
        Awaiter(Awaiter&&) noexcept = default;
        Awaiter& operator=(Awaiter&&) noexcept = delete;
        ~Awaiter() = default;

        bool poll(Context& cx) {
            if (m_token->isCancelled()) {
                return true;
            }

            if (!m_notified) {
                m_notified.emplace(m_token->m_notify.notified());
                if (m_notified->poll(cx)) {
                    return true;
                }
            }

            return m_token->isCancelled();
        }

    private:
        CancellationToken* m_token;
        std::optional<Notified> m_notified;
    };

    Awaiter waitCancelled() noexcept {
        return Awaiter{this};
    }

private:
    std::atomic<bool> m_cancelled{false};
    arc::Notify m_notify;
};

}