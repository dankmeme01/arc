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
struct PollableUniBase;

struct TaskContext {
    Waker* m_waker = nullptr;
    Runtime* m_runtime = nullptr;

    TaskBase* currentTask();
    Runtime* runtime();
    void wake();
    Waker cloneWaker();

    void setTaskDeadline(asp::time::Instant deadline);
    bool shouldCoopYield();

    void pushFrame(const PollableUniBase* pollable);
    void popFrame();
    void markFrame(std::string name);
    void markFrameFromSource(const std::source_location& loc = std::source_location::current());

    void printFutureStack();
    void onUnhandledException(std::exception_ptr ptr);
    void maybeRethrow();

private:
    friend class Runtime;
    struct StackEntry {
        const PollableUniBase* pollable;
        std::string name;
    };

    std::optional<asp::time::Instant> m_taskDeadline;
    size_t m_futurePolls = 0;
    std::vector<StackEntry> m_stack;
    std::vector<std::string> m_capturedStack;
    std::optional<std::exception_ptr> m_currentException;

    void dumpStack();
    void captureStack();

};

inline TaskContext& ctx() {
    static thread_local TaskContext context;
    return context;
}

/// Sets a debugging name for the current Future.
/// This helps with debugging, as the name will be included in the stack trace during unhandled exceptions.
/// The macro ARC_FRAME() can be used instead to automatically set the name to function name and file + line information.
inline void markFrame(std::string name) {
    ctx().markFrame(std::move(name));
}

#define ARC_FRAME() \
    ::arc::ctx().markFrameFromSource()

}