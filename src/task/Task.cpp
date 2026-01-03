#include <arc/task/Task.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

void TaskBase::schedule() noexcept {
    m_vtable->schedule(this);
}

void TaskBase::abort() noexcept {
    auto state = this->getState();

    while (true) {
        // cannot cancel if already completed or closed
        if (state & (TASK_COMPLETED | TASK_CLOSED)) {
            break;
        }

        // if not scheduled nor running, schedule the task
        auto newState = state | TASK_CLOSED;
        if ((state & (TASK_SCHEDULED | TASK_RUNNING)) == 0) {
            newState |= TASK_SCHEDULED;
            newState += TASK_REFERENCE;
        }

        if (this->exchangeState(state, newState)) {
            // schedule it so the future gets dropped by the executor
            if ((state & (TASK_SCHEDULED | TASK_RUNNING)) == 0) {
                this->schedule();
            }

            // notify awaiter
            if (state & TASK_AWAITER) {
                this->notifyAwaiter();
            }

            break;
        }
    }
}

std::optional<bool> TaskBase::pollTask() {
    auto& cx = ctx();
    auto state = this->getState();

    trace("[Task {}] polling, cx waker: {}, state: {}", (void*)this, cx.m_waker ? cx.m_waker->m_data : nullptr, state);

    while (true) {
        // if the task was closed, notify awaiter and return
        if (state & TASK_CLOSED) {
            // if the task is scheduled or running, we need to wait until the future is destroyed
            if (state & (TASK_SCHEDULED | TASK_RUNNING)) {
                // replace the waker
                if (cx.m_waker) this->registerAwaiter(*cx.m_waker);

                // reload the state after registering, to be aware of any changes
                state = this->getState();

                // if the task is still scheduled or running, return pending
                if (state & (TASK_SCHEDULED | TASK_RUNNING)) {
                    return std::nullopt;
                }
            }

            // notify awaiter
            this->notifyAwaiter(cx.m_waker);
            return std::optional{false};
        }

        // if the task is not completed, register waker and return pending
        if ((state & TASK_COMPLETED) == 0) {
            if (cx.m_waker) this->registerAwaiter(*cx.m_waker);

            // reload state
            state = this->getState();

            if (state & TASK_CLOSED) {
                continue;
            }

            // still not completed, return pending
            if ((state & TASK_COMPLETED) == 0) {
                return std::nullopt;
            }
        }

        // task is now completed, try to set closed flag

        if (this->exchangeState(state, state | TASK_CLOSED)) {
            // notify awaiter
            if (state & TASK_AWAITER) {
                this->notifyAwaiter(cx.m_waker);
            }

            // return ready
            return std::optional{true};
        }
    }
}

void TaskBase::vSchedule(void* self) {
    // trace("[Task {}] scheduling", self);

    auto task = static_cast<TaskBase*>(self);
    auto rt = task->m_runtime.lock();
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

std::optional<Waker> TaskBase::takeAwaiter(const Waker* current) {
    auto state = m_state.fetch_or(TASK_NOTIFYING, std::memory_order::acq_rel);

    std::optional<Waker> out;
    if ((state & (TASK_NOTIFYING | TASK_REGISTERING)) == 0) {
        out = std::move(m_awaiter);
        m_awaiter.reset();
        m_state.fetch_and(~TASK_NOTIFYING & ~TASK_AWAITER, std::memory_order::release);

        if (out) {
            if (current && out->equals(*current)) {
                out.reset();
            }
        }
    }

    return out;
}

void TaskBase::registerAwaiter(Waker& waker) {
    trace("[Task {}] registering waker {}", (void*)this, waker.m_data);

    auto state = m_state.fetch_or(0, std::memory_order::acquire);

    while (true) {
        // if we are notifying, wake and return without registering
        if (state & TASK_NOTIFYING) {
            waker.wakeByRef();
            return;
        }

        // mark the state to let other threads know we are registering
        auto newState = state | TASK_REGISTERING;
        if (this->exchangeState(state, newState)) {
            state = newState;
            break;
        }
    }

    // store the awaiter
    m_awaiter = waker.clone();

    std::optional<Waker> w;

    while (true) {
        // if there was a notification, take out the awaiter
        if (state & TASK_NOTIFYING) {
            w = std::move(m_awaiter);
            m_awaiter->reset();
        }

        // the new state is not being notified nor registering, but there might be an awaiter
        auto newState = (state & ~TASK_NOTIFYING & ~TASK_REGISTERING);
        if (w) {
            newState &= ~TASK_AWAITER;
        } else {
            newState |= TASK_AWAITER;
        }

        if (this->exchangeState(state, newState)) {
            break;
        }
    }

    // if there was a notification while registering, wake the awaiter
    if (w) {
        w->wake();
    }
}

void TaskBase::notifyAwaiter(Waker* current) {
    trace("[Task {}] notifying waker {} (cur: {})", (void*)this, m_awaiter ? m_awaiter->m_data : nullptr, current->m_data);

    auto w = this->takeAwaiter(current);

    if (w) {
        w->wake();
    }
}

}