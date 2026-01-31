#pragma once
#include <std23/move_only_function.h>
#include <std23/function_ref.h>
#include <functional>

namespace arc {
#ifdef _WIN32
    template <class Signature>
    using MoveOnlyFunction = std::move_only_function<Signature>;
#else
    template <class Signature>
    using MoveOnlyFunction = std23::move_only_function<Signature>;
#endif

    template <class Signature>
    using FunctionRef = std23::function_ref<Signature>;
}
