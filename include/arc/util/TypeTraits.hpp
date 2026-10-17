#pragma once
#include <type_traits>

namespace arc {

/// Marker tag usually used as a default template argument to specify that the return type of a function should be inferred
struct InferReturn {};

namespace detail {

template <typename T, typename F, typename... Args>
struct DoInferReturn {
    using type = T;
};

template <typename F, typename... Args>
struct DoInferReturn<InferReturn, F, Args...> {
    using type = std::invoke_result_t<F>;
};

}

template <typename T, typename F, typename... Args>
using DoInferReturnType = typename detail::DoInferReturn<T, F, Args...>::type;

}
