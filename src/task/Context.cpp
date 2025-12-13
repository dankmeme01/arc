#include <arc/task/Context.hpp>
#include <arc/task/Task.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

TaskBase* TaskContext::currentTask() {
    if (m_waker) {
        auto raw = m_waker->m_data;
        return static_cast<TaskBase*>(raw);
    }
    return nullptr;
}

Runtime* TaskContext::runtime() {
    if (m_runtime) return m_runtime;

    auto task = this->currentTask();
    if (task) {
        return task->m_runtime;
    }

    return nullptr;
}

void TaskContext::wake() {
    m_waker->wakeByRef();
}

Waker TaskContext::cloneWaker() {
    ARC_DEBUG_ASSERT(m_waker, "no current waker");
    return m_waker->clone();
}

void TaskContext::pushFrame(const PollableUniBase* pollable) {
    // trace("pushing frame {}", (void*)pollable);
    m_stack.push_back(pollable);
}

void TaskContext::popFrame() {
    ARC_DEBUG_ASSERT(!m_stack.empty(), "popFrame() called on empty future stack");
    // trace("popping frame {}", (void*)m_stack.back());
    m_stack.pop_back();
}

void TaskContext::onUnhandledException() {
    // capture the stack trace, as later when dumpStack() is invoked,
    // futures might already be destroyed and we will run into UB
    if (!m_capturedStack.empty()) {
        return;
    }
}

void TaskContext::printFutureStack() {
    auto prevStack = std::move(m_capturedStack);
    this->captureStack();
    this->dumpStack();
    m_capturedStack = std::move(prevStack);
}

void TaskContext::captureStack() {
    m_capturedStack.clear();

    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        auto pollable = *it;
        auto meta = pollable->m_vtable->metadata;

        if (meta->isFuture) {
            struct DummyFuture : Future<> {};

            auto handle = reinterpret_cast<const DummyFuture*>(pollable)->m_handle;
            m_capturedStack.push_back(fmt::format("{} (handle: {})", meta->typeName, (void*)handle.address()));
        } else {
            m_capturedStack.push_back(fmt::format("{}", meta->typeName));
        }
    }
}

void TaskContext::dumpStack() {
    printError("=== Future stack trace ===");
    if (!m_capturedStack.empty()) {
        for (const auto& line : m_capturedStack) {
            printError(" - {}", line);
        }
        return;
    }

    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        auto pollable = *it;
        printError(" - <unknown pollable at {}>", (void*)pollable);
    }

    printError("NOTE: captured stack trace was unavailable.");
}

}