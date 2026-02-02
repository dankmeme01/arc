#include <arc/future/Context.hpp>
#include <arc/task/Task.hpp>
#include <arc/util/Assert.hpp>

using namespace asp::time;

namespace arc {

Context::Context(Waker* waker) : Context(waker, nullptr) {}

Context::Context(Waker* waker, Runtime* runtime)
    : m_waker(waker), m_runtime(runtime) {}

Waker* Context::waker() {
    return m_waker;
}

TaskBase* Context::currentTask() {
    if (m_waker) {
        auto raw = m_waker->m_data;
        return static_cast<TaskBase*>(raw);
    }
    return nullptr;
}

Waker Context::cloneWaker() {
    ARC_ASSERT(m_waker, "no current waker");
    return m_waker->clone();
}

Runtime* Context::runtime() {
    return m_runtime;
}

void Context::wake() {
    m_waker->wakeByRef();
}

void Context::_installWaker(Waker* waker) {
    m_waker = waker;
}

void Context::setTaskDeadline(Instant deadline) {
    m_taskDeadline = deadline.rawNanos();
    m_futurePolls = 0;
}

bool Context::shouldCoopYield() {
    // try to make this check as cheap as possible
    m_futurePolls++;
    if (m_futurePolls % 64 == 0) {
        return m_taskDeadline > 0 && Instant::now().rawNanos() >= m_taskDeadline;
    }
    return false;
}

void Context::pushFrame(const PollableBase* pollable) {
    // trace("pushing frame {}", (void*)pollable);
    m_stack.push_back(StackEntry { pollable, "" });
}

void Context::popFrame() {
    ARC_DEBUG_ASSERT(!m_stack.empty(), "popFrame() called on empty future stack");
    // trace("popping frame {}", (void*)m_stack.back());
    m_stack.pop_back();
}

void Context::markFrame(std::string name) {
    if (m_stack.empty()) {
        return;
    }

    m_stack.back().name = std::move(name);
}

void Context::markFrameFromSource(const std::source_location& loc) {
    this->markFrame(fmt::format("{} ({}:{})", loc.function_name(), loc.file_name(), loc.line()));
}

void Context::printFutureStack() {
    auto prevStack = std::move(m_capturedStack);
    this->captureStack();
    this->dumpStack();
    m_capturedStack = std::move(prevStack);
}

void Context::onUnhandledException(std::exception_ptr ptr) {
    // capture the stack trace, as later when dumpStack() is invoked,
    // futures might already be destroyed and we will run into UB
    if (m_capturedStack.empty()) {
        this->captureStack();
    }
    printWarn("Captured exception (valid: {})", !!ptr);
    if (ptr) {
        m_currentException = ptr;
    } else {
        m_currentException = std::make_exception_ptr(std::runtime_error("unknown exception (null when capturing)"));
    }
}

void Context::maybeRethrow() {
    if (auto exc = std::exchange(m_currentException, nullptr)) {
        std::rethrow_exception(exc);
    }
}

void Context::captureStack() {
    m_capturedStack.clear();

    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        auto pollable = it->pollable;
        auto marker = it->name;
        auto meta = pollable->m_vtable->m_metadata;

        std::string description{marker};
        if (description.empty()) {
            description = meta->typeName;
        }

        if (meta->isFuture) {
            struct DummyFuture : Future<> {
                handle_type handle() const { return m_handle; }
            };

            auto handle = reinterpret_cast<const DummyFuture*>(pollable)->handle();
            description += fmt::format(" (handle: {})", (void*)handle.address());
        }

        m_capturedStack.push_back(std::move(description));
    }
}

void Context::dumpStack() {
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