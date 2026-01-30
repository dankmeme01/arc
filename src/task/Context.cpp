#include <arc/task/Context.hpp>
#include <arc/task/Task.hpp>
#include <arc/util/Assert.hpp>

using namespace asp::time;

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
        auto rt = task->m_runtime.lock();
        return rt.get();
    }

    return nullptr;
}

void TaskContext::wake() {
    m_waker->wakeByRef();
}

Waker TaskContext::cloneWaker() {
    ARC_ASSERT(m_waker, "no current waker");
    return m_waker->clone();
}

void TaskContext::setTaskDeadline(Instant deadline) {
    m_taskDeadline = deadline;
    m_futurePolls = 0;
}

bool TaskContext::shouldCoopYield() {
    // try to make this check as cheap as possible
    m_futurePolls++;
    if (m_futurePolls % 64 == 0) {
        return m_taskDeadline && Instant::now() >= *m_taskDeadline;
    }
    return false;
}

void TaskContext::pushFrame(const PollableUniBase* pollable) {
    // trace("pushing frame {}", (void*)pollable);
    m_stack.push_back(StackEntry { pollable, "" });
}

void TaskContext::popFrame() {
    ARC_DEBUG_ASSERT(!m_stack.empty(), "popFrame() called on empty future stack");
    // trace("popping frame {}", (void*)m_stack.back());
    m_stack.pop_back();
}

void TaskContext::markFrame(std::string name) {
    if (m_stack.empty()) {
        return;
    }

    m_stack.back().name = std::move(name);
}

void TaskContext::markFrameFromSource(const std::source_location& loc) {
    this->markFrame(fmt::format("{} ({}:{})", loc.function_name(), loc.file_name(), loc.line()));
}

void TaskContext::onUnhandledException(std::exception_ptr ptr) {
    // capture the stack trace, as later when dumpStack() is invoked,
    // futures might already be destroyed and we will run into UB
    if (m_capturedStack.empty()) {
        this->captureStack();
    }
    printWarn("Captured exception (valid: {})", !!ptr);
    m_currentException = ptr;
}

void TaskContext::maybeRethrow() {
    if (m_currentException) {
        auto exc = *m_currentException;
        m_currentException = std::nullopt;

        if (exc) {
            std::rethrow_exception(exc);
        } else {
            throw std::runtime_error("unknown exception, null when rethrowing");
        }
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
        auto pollable = it->pollable;
        auto marker = it->name;
        auto meta = pollable->m_vtable->metadata;

        std::string description{marker};
        if (description.empty()) {
            description = meta->typeName;
        }

        if (meta->isFuture) {
            struct DummyFuture : Future<> {};
            auto handle = reinterpret_cast<const DummyFuture*>(pollable)->m_handle;
            description += fmt::format(" (handle: {})", (void*)handle.address());
        }

        m_capturedStack.push_back(std::move(description));
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
        auto pollable = it->pollable;
        auto marker = it->name;

        if (marker.empty()) {
            printError(" - <unknown pollable @ {}>", (void*)pollable);
        } else {
            printError(" - {} (@ {})", marker, (void*)pollable);
        }
    }

    printError("NOTE: captured stack trace was unavailable.");
}

}