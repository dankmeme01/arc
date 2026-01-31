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
    Context(Waker* waker, Runtime* runtime);

    Waker* waker();
    TaskBase* currentTask();
    Waker cloneWaker();
    Runtime* runtime();
    void wake();

    void _installWaker(Waker* waker);

    void setTaskDeadline(asp::Instant deadline);
    bool shouldCoopYield();

    void pushFrame(const PollableBase* pollable);
    void popFrame();
    void markFrame(std::string name);
    void markFrameFromSource(const std::source_location& loc = std::source_location::current());

    void printFutureStack();
    void onUnhandledException(std::exception_ptr ptr);
    void maybeRethrow();

private:
    friend class Runtime;

    struct StackEntry {
        const PollableBase* pollable;
        std::string name;
    };

    Waker* m_waker;
    Runtime* m_runtime;
    std::optional<asp::time::Instant> m_taskDeadline;
    size_t m_futurePolls = 0;
    std::vector<StackEntry> m_stack;
    std::vector<std::string> m_capturedStack;
    std::optional<std::exception_ptr> m_currentException;

    void dumpStack();
    void captureStack();
};

/// Sets a debugging name for the current Future.
/// This helps with debugging, as the name will be included in the stack trace during unhandled exceptions.
/// The macro ARC_FRAME() can be used instead to automatically set the name to function name and file + line information.
inline void markFrame(Context& cx, std::string name) {
    cx.markFrame(std::move(name));
}

#define ARC_FRAME() \
    ((co_await ::arc::PromiseBase::current())->getContext()->markFrameFromSource())

}