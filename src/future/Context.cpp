#include <arc/future/Context.hpp>
#include <arc/task/Task.hpp>
#include <arc/util/Assert.hpp>

constexpr size_t MAX_RECURSION_DEPTH = 512;

using namespace asp::time;

namespace arc {

Context::Context(Waker* waker) : Context(waker, nullptr) {}

Context::Context(Waker* waker, Runtime* runtime)
    : m_waker(waker), m_runtime(runtime) {}

Waker* Context::waker() noexcept {
    return m_waker;
}

TaskBase* Context::currentTask() noexcept {
    if (m_waker) {
        auto raw = m_waker->m_data;
        return static_cast<TaskBase*>(raw);
    }
    return nullptr;
}

Waker Context::cloneWaker() const noexcept {
    ARC_ASSERT(m_waker, "no current waker");
    return m_waker->clone();
}

Runtime* Context::runtime() noexcept {
    return m_runtime;
}

void Context::wake() noexcept {
    m_waker->wakeByRef();
}

void Context::_installWaker(Waker* waker) noexcept {
    m_waker = waker;
}


void Context::setup(Instant taskDeadline) noexcept {
    m_taskDeadline = taskDeadline.rawNanos();
    m_futurePolls = 0;
    m_unused = nullptr;
    m_stack.clear();
    m_capturedStack.clear();
    m_stack.reserve(32);
}

bool Context::shouldCoopYield() noexcept {
    // try to make this check as cheap as possible
    m_futurePolls++;
    if (m_futurePolls % 64 == 0) {
        return m_taskDeadline > 0 && Instant::now().rawNanos() >= m_taskDeadline;
    }
    return false;
}

void Context::pushFrame(const PollableBase* pollable) {
    // trace("pushing frame {}", (void*)pollable);
    m_stack.push_back(StackEntry { pollable, {} });
    ARC_DEBUG_ASSERT(m_stack.size() < MAX_RECURSION_DEPTH, "maximum future recursion depth exceeded");
}

void Context::popFrame() noexcept {
    ARC_DEBUG_ASSERT(!m_stack.empty(), "popFrame() called on empty future stack");
    // trace("popping frame {}", (void*)&m_stack.back());
    m_stack.pop_back();
}

void Context::markFrame(asp::UniqueBoxedString name) noexcept {
    if (m_stack.empty()) {
        return;
    }

    m_stack.back().name = std::move(name);
}

void Context::markFrameFromSource(const std::source_location& loc) {
    this->markFrame(asp::UniqueBoxedString::format("{} ({}:{})", loc.function_name(), loc.file_name(), loc.line()));
}

void Context::printFutureStack() {
    auto prevStack = std::move(m_capturedStack);
    this->captureStack();
    this->dumpStack();
    m_capturedStack = std::move(prevStack);
}

void Context::onUnhandledException() {
    // capture the stack trace, as later when dumpStack() is invoked,
    // futures might already be destroyed and we will run into UB
    if (m_capturedStack.empty()) {
        this->captureStack();
    }
}

void Context::captureStack() {
    m_capturedStack.clear();

    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        auto pollable = it->pollable;
        auto& marker = it->name;
        auto meta = pollable->m_vtable->m_metadata;

        std::string description{marker.view()};
        if (description.empty() && meta) {
            description = meta->typeName;
        }

        if (meta && meta->isFuture) {
            struct DummyFuture : Future<> {
                handle_type handle() const { return m_handle; }
            };

            auto handle = reinterpret_cast<const DummyFuture*>(pollable)->handle();
            description += fmt::format(" (handle: {})", (void*)handle.address());
        }

        if (description.empty()) {
            // no metadata available
            description = fmt::format("<unknown pollable @ {}>", (void*)pollable);
        }

        m_capturedStack.push_back(asp::UniqueBoxedString{description});
    }

    trace("Captured {} frames", m_capturedStack.size());
}

void Context::dumpStack() {
    printError("=== Future stack trace (most recent call first) ===");
    if (!m_capturedStack.empty()) {
        for (const auto& line : m_capturedStack) {
            printError(" - {}", line);
        }
        return;
    }

    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        auto pollable = it->pollable;
        auto& marker = it->name;

        if (marker.empty()) {
            printError(" - <unknown pollable @ {}>", (void*)pollable);
        } else {
            printError(" - {} (@ {})", marker, (void*)pollable);
        }
    }

    printError("NOTE: captured stack trace was unavailable.");
}

}