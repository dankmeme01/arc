#pragma once
#include <vector>
#include <string>
#include <asp/time/Instant.hpp>

namespace arc {

struct Waker;
class Runtime;
struct TaskBase;
struct PollableUniBase;

struct TaskContext {
    Waker* m_waker = nullptr;
    Runtime* m_runtime = nullptr;
    std::vector<const PollableUniBase*> m_stack;
    std::vector<std::string> m_capturedStack;
    asp::time::Instant m_taskRanAt;
    size_t m_futurePolls = 0;

    TaskBase* currentTask();
    Runtime* runtime();
    void wake();
    Waker cloneWaker();

    void recordTaskRanNow();
    bool shouldCoopYield();

    void pushFrame(const PollableUniBase* pollable);
    void popFrame();

    void printFutureStack();
    void onUnhandledException();

private:
    friend class Runtime;

    void dumpStack();
    void captureStack();

};

inline TaskContext& ctx() {
    static thread_local TaskContext context;
    return context;
}

}