#include <arc/task/CancellationToken.hpp>

namespace arc {

bool CancellationToken::Awaiter::poll(Context& cx) {
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

}