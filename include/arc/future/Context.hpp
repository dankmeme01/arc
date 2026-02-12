#pragma once
#include <vector>
#include <string>
#include <exception>
#include <source_location>
#include <asp/time/Instant.hpp>
#include <fmt/core.h>

namespace arc {

struct Waker;
class Runtime;
struct TaskBase;
struct PollableBase;

class Context {
public:
    Context(Waker* waker);
    Context(Waker* waker, Runtime* runtime);

    Waker* waker() noexcept;
    TaskBase* currentTask() noexcept;
    Waker cloneWaker();
    Runtime* runtime() noexcept;
    void wake() noexcept;

    void _installWaker(Waker* waker) noexcept;

    bool shouldCoopYield() noexcept;

    void pushFrame(const PollableBase* pollable);
    void popFrame() noexcept;
    void markFrame(std::string name) noexcept;
    void markFrameFromSource(const std::source_location& loc = std::source_location::current());

    void printFutureStack();
    void dumpStack();
    void onUnhandledException();

private:
    friend class Runtime;

    /// Setup the context for a new task execution.
    void setup(asp::Instant taskDeadline) noexcept;

    struct StackEntry {
        const PollableBase* pollable;
        std::string name;
    };

    Waker* m_waker;
    Runtime* m_runtime;
    uint32_t m_futurePolls = 0;
    uint64_t m_taskDeadline = 0;
    std::exception_ptr m_unused;
    std::vector<StackEntry> m_stack;
    std::vector<std::string> m_capturedStack;
    // -- all fields above are expected to be stable and not change --

    void captureStack();
};

/// Sets a debugging name for the current Future.
/// This helps with debugging, as the name will be included in the stack trace during unhandled exceptions.
/// The macro ARC_FRAME() can be used instead to automatically set the name to function name and file + line information.
inline void markFrame(Context& cx, std::string name) noexcept {
    cx.markFrame(std::move(name));
}

#define ARC_FRAME() \
    ((co_await ::arc::PromiseBase::current())->getContext()->markFrameFromSource())

}
