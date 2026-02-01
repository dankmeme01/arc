#pragma once
#include <utility>

namespace arc {

template <typename F>
auto scopeDtor(F&& func) {
    struct ScopeDtor {
        std::decay_t<F> m_func;
        bool m_invoke = true;

        ~ScopeDtor() {
            if (m_invoke) m_func();
        }

        void cancel() noexcept {
            m_invoke = false;
        }
    };

    return ScopeDtor{std::forward<F>(func)};
}

}
