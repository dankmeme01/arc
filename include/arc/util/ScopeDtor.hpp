#pragma once
#include <utility>

namespace arc {

template <typename F>
auto scopeDtor(F&& func) {
    struct ScopeDtor {
        std::decay_t<F> m_func;

        ~ScopeDtor() {
            m_func();
        }
    };

    return ScopeDtor{std::forward<F>(func)};
}

}
