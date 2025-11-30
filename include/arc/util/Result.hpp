#pragma once
#include <Geode/Result.hpp>

#define ARC_UNWRAP(...) GEODE_UNWRAP(__VA_ARGS__)
#define ARC_UNWRAP_INTO(...) GEODE_UNWRAP_INTO(__VA_ARGS__)

#if defined(__GNUC__) || defined(__clang__)
    #define ARC_CO_UNWRAP(...)                                                          \
        ({                                                                             \
            auto GEODE_CONCAT(res, __LINE__) = __VA_ARGS__;                            \
            if (GEODE_CONCAT(res, __LINE__).isErr())                                   \
                co_return geode::Err(std::move(GEODE_CONCAT(res, __LINE__)).unwrapErr()); \
            std::move(GEODE_CONCAT(res, __LINE__)).unwrap();                           \
        })
#else
    #define ARC_CO_UNWRAP(...) \
        if (auto res = __VA_ARGS__; res.isErr()) co_return geode::Err(std::move(res).unwrapErr())
#endif

#define ARC_CO_UNWRAP_INTO(variable, ...)                                       \
    auto GEODE_CONCAT(res, __LINE__) = __VA_ARGS__;                            \
    if (GEODE_CONCAT(res, __LINE__).isErr())                                   \
        co_return geode::Err(std::move(GEODE_CONCAT(res, __LINE__)).unwrapErr()); \
    variable = std::move(GEODE_CONCAT(res, __LINE__)).unwrap()