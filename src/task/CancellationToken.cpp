#include <arc/task/CancellationToken.hpp>

namespace arc {

bool CancellationToken::Awaiter::poll() {
    if (m_token->isCancelled()) {
        return true;
    }

    if (!m_notified) {
        m_notified.emplace(m_token->m_notify.notified());
    }

    return m_token->isCancelled();
}

}