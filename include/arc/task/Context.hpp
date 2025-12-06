#pragma once

namespace arc {

struct Waker;
class Runtime;
struct TaskBase;

struct TaskContext {
    Waker* m_waker = nullptr;

    TaskBase* currentTask();
    Runtime* runtime();
    void wake();
    Waker cloneWaker();
};

inline TaskContext& ctx() {
    static thread_local TaskContext context;
    return context;
}

}