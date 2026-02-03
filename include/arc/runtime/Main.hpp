#include "Runtime.hpp"
#include <Geode/Result.hpp>
#include <optional>
#include <cstddef>

namespace arc {

inline int _mainWrapper(int argc, char** argv, auto mainFut, std::optional<size_t> numThreads) {
    const char** cargv = const_cast<const char**>(argv);
    int ret = 0;

    auto runtime = arc::Runtime::create(numThreads.value_or(std::thread::hardware_concurrency()));

    auto runVoidMain = [&](auto&& fut) {
        runtime->blockOn(std::forward<std::decay_t<decltype(fut)>>(fut));
    };

    auto runNVMain = [&](auto&& fut) {
        decltype(auto) mainRet = runtime->blockOn(std::forward<std::decay_t<decltype(fut)>>(fut));

        if constexpr (std::is_convertible_v<decltype(mainRet), int>) {
            ret = static_cast<int>(mainRet);
        } else if constexpr (geode::IsResult<decltype(mainRet)>) {
            if (mainRet.isErr()) {
                auto err = std::move(mainRet).unwrapErr();
                printError("arc main terminated with error: {}", err);
                ret = 1;
            }
        } else {
            static_assert(!std::is_same_v<decltype(mainRet), decltype(mainRet)>, "main function has invalid return type, see readme");
        }
    };

    if constexpr (requires { mainFut(argc, argv); }) {
        using MainOutput = typename ::arc::FutureTraits<decltype(mainFut(argc, argv))>::Output;
        constexpr bool IsVoid = std::is_void_v<MainOutput>;

        if constexpr (IsVoid) {
            runVoidMain(mainFut(argc, argv));
        } else {
            runNVMain(mainFut(argc, argv));
        }
    } else if constexpr (requires { mainFut(argc, cargv); }) {
        using MainOutput = typename ::arc::FutureTraits<decltype(mainFut(argc, cargv))>::Output;
        constexpr bool IsVoid = std::is_void_v<MainOutput>;

        if constexpr (IsVoid) {
            runVoidMain(mainFut(argc, cargv));
        } else {
            runNVMain(mainFut(argc, cargv));
        }
    } else if constexpr (requires { mainFut(); }) {
        using MainOutput = typename ::arc::FutureTraits<decltype(mainFut())>::Output;
        constexpr bool IsVoid = std::is_void_v<MainOutput>;

        if constexpr (IsVoid) {
            runVoidMain(mainFut());
        } else {
            runNVMain(mainFut());
        }
    } else {
        static_assert(!std::is_same_v<decltype(mainFut), decltype(mainFut)>, "main function has invalid signature, see readme");
    }

    runtime->safeShutdown();

    trace("arc main wrapper exiting with code {}", ret);

    return ret;
}

}

#define ARC_DEFINE_MAIN(f) \
    int main(int argc, char** argv) { \
        return ::arc::_mainWrapper(argc, argv, f, std::nullopt); \
    }

#define ARC_DEFINE_MAIN_NT(f, nt) \
    int main(int argc, char** argv) { \
        return ::arc::_mainWrapper(argc, argv, f, nt); \
    }
