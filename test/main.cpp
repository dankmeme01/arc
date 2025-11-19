// #include <arc/prelude.hpp>
#include <arc/prelude.hpp>
#include <asp/time.hpp>

#include <fmt/format.h>

using namespace asp::time;

#define DBG_FUT_WRAP(f) ({ \
    auto _fut = f; \
    _fut.setDebugName("# " #f); \
    std::move(_fut); \
})

#define dbg_await(f) { auto fut = DBG_FUT_WRAP(f); co_await fut; }

arc::Future<> recurseWait(int level) {
    if (level == 0) co_return;
    co_await(arc::sleepFor(Duration::fromMillis(100)));
    arc::trace("recurseWait level {} IN", level);
    dbg_await(recurseWait(level - 1));
    arc::trace("recurseWait level {} OUT", level);
}

arc::Future<int> waiter(Duration dur) {
    arc::trace("{} is waiting for {}", pthread_self(), dur.toString());
    co_await arc::sleepFor(dur);
    arc::trace("{} has finished waiting for {}", pthread_self(), dur.toString());
    co_await arc::yield();
    co_return dur.millis() + 42;
}

arc::Future<int> noop(int x) {
    if (x > 0) {
        co_await noop(x - 1);
    }
    co_return x;
}

arc::Future<int> locker(arc::Mutex<>& mtx) {
    arc::trace("locker trying to lock mutex");
    auto guard = co_await mtx.lock();
    arc::trace("locker acquired mutex");
    co_await arc::sleepFor(Duration::fromMillis(500));
    arc::trace("locker releasing mutex");
    co_return 478;
}

arc::Future<> asyncMain() {
    arc::trace("Hello from asyncMain!");
    arc::Notify notify;
    arc::Mutex<> mtx;

    int x = co_await arc::spawn(locker(mtx));

    {
        auto mg = co_await mtx.lock();
        fmt::println("Locked mutex in asyncMain {}", x);
        co_await arc::sleepFor(Duration::fromMillis(200));
        fmt::println("Releasing mutex in asyncMain");
    }


    dbg_await(arc::select(
        // future that finishes after 2.5 seconds
        arc::selectee(
            arc::sleepFor(Duration::fromMillis(2500)),
            []() { fmt::println("2.5 seconds elapsed, shutting down!"); }
        ),

        // future that never completes
        arc::selectee(
            arc::never(),
            []() { fmt::println("this will never happen"); }
        ),

        // future that waits for ctrl+c signal
        arc::selectee(
            arc::ctrl_c(),
            [] { fmt::println("Ctrl+C received, exiting!"); }
        )
    ));

    co_return;
}

ARC_DEFINE_MAIN(asyncMain);
