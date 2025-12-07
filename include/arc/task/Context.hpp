#pragma once
#include <vector>
#include <string>

namespace arc {

struct Waker;
class Runtime;
struct TaskBase;
struct PollableUniBase;

struct TaskContext {
    Waker* m_waker = nullptr;
    std::vector<const PollableUniBase*> m_stack;
    std::vector<std::string> m_capturedStack;

    TaskBase* currentTask();
    Runtime* runtime();
    void wake();
    Waker cloneWaker();

    void pushFrame(const PollableUniBase* pollable);
    void popFrame();
    void onUnhandledException();
    void dumpStack();
};

inline TaskContext& ctx() {
    static thread_local TaskContext context;
    return context;
}

}