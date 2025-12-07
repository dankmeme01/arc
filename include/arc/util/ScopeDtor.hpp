#pragma once
#include <utility>

namespace arc {

auto scopeDtor(auto&& func) {
    struct ScopeDtor {
        decltype(func) m_func;

        ~ScopeDtor() {
            m_func();
        }
    };

    return ScopeDtor{std::forward<decltype(func)>(func)};
}

}
