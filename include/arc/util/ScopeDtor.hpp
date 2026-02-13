#pragma once
#include <utility>
#include <asp/detail/config.hpp>

namespace arc {

template <typename F>
ASP_FORCE_INLINE auto scopeDtor(F&& func) {
    struct ScopeDtor {
        ASP_FORCE_INLINE ScopeDtor(F&& f) : m_func(std::forward<F>(f)) {}

        ASP_FORCE_INLINE ~ScopeDtor() {
            if (m_invoke) m_func();
        }

        void cancel() noexcept {
            m_invoke = false;
        }
    private:
        std::decay_t<F> m_func;
        bool m_invoke = true;
    };

    return ScopeDtor{std::forward<F>(func)};
}

}
