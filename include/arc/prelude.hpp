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
#include "task/Yield.hpp"
#include "task/Waker.hpp"
#include "time/Sleep.hpp"
#include "time/Interval.hpp"
#include "time/Timeout.hpp"

#define ARC_DEFINE_MAIN(f) \
    int main(int argc, char** argv) { \
        arc::Runtime runtime; \
        runtime.blockOn(f()); \
    }