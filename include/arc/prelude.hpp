#include "future/Future.hpp"
#include "future/Select.hpp"
#include "future/Join.hpp"
#include "future/Pollable.hpp"
#include "net/TcpStream.hpp"
#include "net/TcpListener.hpp"
#include "net/UdpSocket.hpp"
#include "runtime/Runtime.hpp"
#include "signal/Signal.hpp"
#include "sync/mpsc.hpp"
#include "sync/Notify.hpp"
#include "sync/Mutex.hpp"
#include "sync/Semaphore.hpp"
#include "task/Task.hpp"
#include "task/CancellationToken.hpp"
#include "task/CondvarWaker.hpp"
#include "task/Context.hpp"
#include "task/Yield.hpp"
#include "task/Waker.hpp"
#include "time/Sleep.hpp"
#include "time/Interval.hpp"
#include "time/Timeout.hpp"
#include "util/Assert.hpp"
#include "util/Random.hpp"
#include "util/Result.hpp"

namespace arc {

inline int _mainWrapper(int argc, char** argv, auto mainFut, std::optional<size_t> numThreads) {
    arc::Runtime runtime{numThreads.value_or(std::thread::hardware_concurrency())};
    int ret = 0;

    if constexpr (requires { mainFut(argc, argv); }) {
        using MainOutput = typename ::arc::FutureTraits<decltype(mainFut(argc, argv))>::Output;
        constexpr bool IsVoid = std::is_void_v<MainOutput>;

        if constexpr (IsVoid) {
            runtime.blockOn(mainFut(argc, argv));
        } else {
            ret = runtime.blockOn(mainFut(argc, argv));
        }
    } else {
        using MainOutput = typename ::arc::FutureTraits<decltype(mainFut())>::Output;
        constexpr bool IsVoid = std::is_void_v<MainOutput>;

        if constexpr (IsVoid) {
            runtime.blockOn(mainFut());
        } else {
            ret = runtime.blockOn(mainFut());
        }
    }

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