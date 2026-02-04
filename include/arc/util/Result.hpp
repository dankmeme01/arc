#pragma once
#include <Geode/Result.hpp>

#define ARC_UNWRAP(...) GEODE_UNWRAP(__VA_ARGS__)
#define ARC_UNWRAP_INTO(...) GEODE_UNWRAP_INTO(__VA_ARGS__)
#define ARC_MAP_UNWRAP(...) GEODE_UNWRAP((__VA_ARGS__).mapErr([](auto err) { return err.message(); }))
#define ARC_CO_UNWRAP(...) GEODE_CO_UNWRAP(__VA_ARGS__)
#define ARC_CO_UNWRAP_INTO(...) GEODE_CO_UNWRAP_INTO(__VA_ARGS__)
#define ARC_CO_MAP_UNWRAP(...) GEODE_CO_UNWRAP((__VA_ARGS__).mapErr([](auto err) { return err.message(); }))

namespace arc {

using geode::Ok;
using geode::Err;
using geode::Result;

}