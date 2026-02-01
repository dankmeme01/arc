#include <arc/task/Task.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

#define $safe_member(where, member) \
    if (offsetof(TaskDebugData, member) >= m_debugDataSize) return {}; \
    auto& where = member;

asp::Duration TaskDebugData::totalRuntime() const noexcept {
    $safe_member(nanos, m_runtimeNs);
    return asp::Duration::fromNanos(nanos.load(std::memory_order::relaxed));
}

uint64_t TaskDebugData::totalPolls() const noexcept {
    $safe_member(polls, m_polls);
    return polls.load(std::memory_order::relaxed);
}

std::string TaskDebugData::name() const noexcept {
    $safe_member(name, m_name);
    return *name.lock();
}
std::stacktrace TaskDebugData::creationStack() const noexcept {
    $safe_member(stack, m_creationStack);
    return stack;
}

void TaskBase::schedule() noexcept {
    m_vtable->schedule(this);
}

void TaskBase::abort() noexcept {
    m_vtable->abort(this, false);
}

void TaskBase::setName(std::string name) noexcept {
    m_vtable->setName(this, std::move(name));
}

asp::SharedPtr<TaskDebugData> TaskBase::getDebugData() noexcept {
    return m_vtable->getDebugData(this);
}

std::optional<bool> TaskBase::vPoll(void* ptr, Context& cx) {
    auto self = static_cast<TaskBase*>(ptr);
    auto state = self->getState();

    trace("[Task {}] polling, cx waker: {}, state: {}", (void*)self, cx.waker() ? cx.waker()->m_data : nullptr, state);

    while (true) {
        // if the task was closed, notify awaiter and return
        if (state & TASK_CLOSED) {
            // if the task is scheduled or running, we need to wait until the future is destroyed
            if (state & (TASK_SCHEDULED | TASK_RUNNING)) {
                // replace the waker
                if (cx.waker()) self->registerAwaiter(*cx.waker());

                // reload the state after registering, to be aware of any changes
                state = self->getState();

                // if the task is still scheduled or running, return pending
                if (state & (TASK_SCHEDULED | TASK_RUNNING)) {
                    return std::nullopt;
                }
            }

            // notify awaiter
            self->notifyAwaiter(cx.waker());
            return std::optional{false};
        }

        // if the task is not completed, register waker and return pending
        if ((state & TASK_COMPLETED) == 0) {
            if (cx.waker()) self->registerAwaiter(*cx.waker());

            // reload state
            state = self->getState();

            if (state & TASK_CLOSED) {
                continue;
            }

            // still not completed, return pending
            if ((state & TASK_COMPLETED) == 0) {
                return std::nullopt;
            }
        }

        // task is now completed, try to set closed flag

        if (self->exchangeState(state, state | TASK_CLOSED)) {
            // notify awaiter
            if (state & TASK_AWAITER) {
                self->notifyAwaiter(cx.waker());
            }

            // return ready
            return std::optional{true};
        }
    }
}

void TaskBase::vAbort(void* ptr, bool force) noexcept {
    auto self = static_cast<TaskBase*>(ptr);

    auto state = self->getState();

    while (true) {
        // cannot cancel if already completed or closed
        if (!force && (state & (TASK_COMPLETED | TASK_CLOSED))) {
            break;
        }

        // if not scheduled nor running, schedule the task
        auto newState = state | TASK_CLOSED;
        if ((state & (TASK_SCHEDULED | TASK_RUNNING)) == 0) {
            newState |= TASK_SCHEDULED;
            newState += TASK_REFERENCE;
        }

        if (self->exchangeState(state, newState)) {
            // schedule it so the future gets dropped by the executor
            if ((state & (TASK_SCHEDULED | TASK_RUNNING)) == 0) {
                self->schedule();
            }

            // notify awaiter
            if (state & TASK_AWAITER) {
                self->notifyAwaiter();
            }

            break;
        }
    }
}

void TaskBase::vSchedule(void* self) {
    // trace("[Task {}] scheduling", self);

    auto task = static_cast<TaskBase*>(self);
    auto rt = task->m_runtime.upgrade();
    if (rt && !rt->isShuttingDown()) {
        rt->enqueueTask(task);
    }
}

void TaskBase::vDropRef(void* self) {
    // trace("[Task {}] dropping reference", self);

    auto task = static_cast<TaskBase*>(self);
    auto state = task->decref();

    if (shouldDestroy(state)) {
        task->m_vtable->destroy(self);
    }
}

void TaskBase::vDropWaker(void* ptr) {
    // trace("[Task {}] dropping waker", ptr);

    auto self = static_cast<TaskBase*>(ptr);
    auto state = self->decref();

    if (shouldDestroy(state)) {
        // if the task was not completed or closed, close it and schedule one more time
        if (state & (TASK_COMPLETED | TASK_CLOSED)) {
            self->m_vtable->destroy(self);
        } else {
            self->setState(TASK_SCHEDULED | TASK_CLOSED | TASK_REFERENCE);
            self->schedule();
        }
    }
}

bool TaskBase::shouldDestroy(uint64_t state) noexcept {
    return (state & ~(TASK_REFERENCE - 1)) == 0 && (state & TASK_TASK) == 0;
}

uint64_t TaskBase::incref() noexcept {
    return m_state.fetch_add(TASK_REFERENCE, std::memory_order::relaxed);
}

uint64_t TaskBase::decref() noexcept {
    return m_state.fetch_sub(TASK_REFERENCE, std::memory_order::acq_rel) - TASK_REFERENCE;
}

void TaskBase::setState(uint64_t newState) noexcept {
    m_state.store(newState, std::memory_order::release);
}

uint64_t TaskBase::getState() noexcept {
    return m_state.load(std::memory_order::acquire);
}

bool TaskBase::exchangeState(uint64_t& expected, uint64_t newState) noexcept {
    return m_state.compare_exchange_weak(expected, newState, std::memory_order::acq_rel, std::memory_order::acquire);
}

void TaskBase::ensureDebugData() {
    if (!m_debugData) {
        m_debugData = asp::make_shared<TaskDebugData>();
        m_debugData->m_task = this;
        m_debugData->m_creationStack = std::stacktrace::current(1);
    }
}

void TaskBase::registerAwaiter(Waker& waker) {
    m_vtable->registerAwaiter(this, waker);
}

void TaskBase::notifyAwaiter(Waker* current) {
    m_vtable->notifyAwaiter(this, current);
}

std::optional<Waker> TaskBase::takeAwaiter(const Waker* current) {
    return m_vtable->takeAwaiter(this, current);
}

std::optional<Waker> TaskBase::vTakeAwaiter(void* ptr, const Waker* current) {
    auto self = static_cast<TaskBase*>(ptr);

    auto state = self->m_state.fetch_or(TASK_NOTIFYING, std::memory_order::acq_rel);

    std::optional<Waker> out;
    if ((state & (TASK_NOTIFYING | TASK_REGISTERING)) == 0) {
        out = std::move(self->m_awaiter);
        self->m_awaiter.reset();
        self->m_state.fetch_and(~TASK_NOTIFYING & ~TASK_AWAITER, std::memory_order::release);

        if (out) {
            if (current && out->equals(*current)) {
                out.reset();
            }
        }
    }

    return out;
}

void TaskBase::vSetName(void* ptr, std::string name) {
    auto self = static_cast<TaskBase*>(ptr);
    self->m_name = std::move(name);
    if (self->m_debugData) {
        *self->m_debugData->m_name.lock() = self->m_name;
    }
}

std::string_view TaskBase::vGetName(void* ptr) {
    auto self = static_cast<TaskBase*>(ptr);
    return self->m_name;
}

asp::SharedPtr<TaskDebugData> TaskBase::vGetDebugData(void* ptr) {
    auto self = static_cast<TaskBase*>(ptr);
    return self->m_debugData;
}

void TaskBase::vRegisterAwaiter(void* ptr, Waker& waker) {
    auto self = static_cast<TaskBase*>(ptr);
    trace("[Task {}] registering waker {}", (void*)self, waker.m_data);

    auto state = self->m_state.fetch_or(0, std::memory_order::acquire);

    while (true) {
        // if we are notifying, wake and return without registering
        if (state & TASK_NOTIFYING) {
            waker.wakeByRef();
            return;
        }

        // mark the state to let other threads know we are registering
        auto newState = state | TASK_REGISTERING;
        if (self->exchangeState(state, newState)) {
            state = newState;
            break;
        }
    }

    // store the awaiter
    self->m_awaiter = waker.clone();

    std::optional<Waker> w;

    while (true) {
        // if there was a notification, take out the awaiter
        if (state & TASK_NOTIFYING) {
            w = std::move(self->m_awaiter);
            self->m_awaiter->reset();
        }

        // the new state is not being notified nor registering, but there might be an awaiter
        auto newState = (state & ~TASK_NOTIFYING & ~TASK_REGISTERING);
        if (w) {
            newState &= ~TASK_AWAITER;
        } else {
            newState |= TASK_AWAITER;
        }

        if (self->exchangeState(state, newState)) {
            break;
        }
    }

    // if there was a notification while registering, wake the awaiter
    if (w) {
        w->wake();
    }
}

void TaskBase::vNotifyAwaiter(void* ptr, Waker* current) {
    auto self = static_cast<TaskBase*>(ptr);
    trace("[Task {}] notifying waker {} (cur: {})", (void*)self, self->m_awaiter ? self->m_awaiter->m_data : nullptr, current->m_data);

    auto w = self->takeAwaiter(current);

    if (w) {
        w->wake();
    }
}

}