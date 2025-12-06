#pragma once

#include "Waker.hpp"
#include "CondvarWaker.hpp"
#include "Context.hpp"
#include <arc/future/Future.hpp>
#include <arc/util/Trace.hpp>
#include <arc/util/ManuallyDrop.hpp>

#if 0
# define TRACE trace
#else
# define TRACE(...) do {} while(0)
#endif

#include <utility>
#include <atomic>
#include <limits>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace arc {

static constexpr uint64_t TASK_SCHEDULED   = 1 << 0;
static constexpr uint64_t TASK_COMPLETED   = 1 << 1;
static constexpr uint64_t TASK_RUNNING     = 1 << 3;
static constexpr uint64_t TASK_CLOSED      = 1 << 4;
static constexpr uint64_t TASK_AWAITER     = 1 << 5;
static constexpr uint64_t TASK_NOTIFYING   = 1 << 6;
static constexpr uint64_t TASK_REGISTERING = 1 << 7;
static constexpr uint64_t TASK_TASK        = 1 << 8;
static constexpr uint64_t TASK_REFERENCE   = 1 << 12;

struct TaskVtable {
    using Fn = void(*)(void*);
    using RunFn = bool(*)(void*);
    using CloneWakerFn = RawWaker(*)(void*);

    Fn schedule;
    Fn dropFuture;
    Fn dropRef;
    Fn destroy;
    RunFn run;
    CloneWakerFn cloneWaker;
};

struct TaskBase {
    std::atomic<uint64_t> m_state{TASK_SCHEDULED | TASK_REFERENCE | TASK_TASK};
    Runtime* m_runtime;
    const TaskVtable* m_vtable;
    std::optional<Waker> m_awaiter;

    void schedule() noexcept;

    /// Polls the task. Returns:
    /// - std::nullopt if the task is pending
    /// - true if the task is completed
    /// - false if the task was closed before completion
    std::optional<bool> pollTask();

    static void vSchedule(void* self);
    static void vDropRef(void* self);
    static void vDropWaker(void* ptr);

protected:
    friend class Runtime;
    template <typename T>
    friend struct TaskHandle;

    static bool shouldDestroy(uint64_t state) noexcept;
    uint64_t incref() noexcept;
    uint64_t decref() noexcept;
    void setState(uint64_t newState) noexcept;
    uint64_t getState() noexcept;
    bool exchangeState(uint64_t& expected, uint64_t newState) noexcept;

    std::optional<Waker> takeAwaiter(const Waker* current = nullptr);
    void registerAwaiter(Waker& waker);
    void notifyAwaiter(Waker* current = nullptr);
};

template <typename T>
struct TaskTypedBase : TaskBase {
    using Output = T;
    static constexpr bool IsVoid = std::is_void_v<Output>;
    using NVOutput = std::conditional_t<IsVoid, std::monostate, Output>;

    std::optional<NVOutput> m_value;

    std::optional<NVOutput> detach() {
        std::optional<NVOutput> out;

        auto state = this->getState();

        // commonly, the task is being detached right after being created, assume this may be the case
        auto expected = TASK_SCHEDULED | TASK_REFERENCE | TASK_TASK;
        if (this->exchangeState(expected, TASK_SCHEDULED | TASK_REFERENCE)) {
            return out;
        }

        // didn't guess, will have to loop
        while (true) {
            if (state & TASK_COMPLETED && (state & TASK_CLOSED) == 0) {
                // mark as closed, grab the output
                if (this->exchangeState(state, state | TASK_CLOSED)) {
                    out = std::move(m_value);
                    state |= TASK_CLOSED;
                }
            } else {
                // if this is the last reference and the task isn't closed, close and schedule again
                auto newState = state & ~TASK_TASK;
                if ((state & (~(TASK_REFERENCE - 1) | TASK_CLOSED)) == 0) {
                    newState = TASK_SCHEDULED | TASK_CLOSED | TASK_REFERENCE;
                }

                if (this->exchangeState(state, newState)) {
                    // if this is the last reference, either schedule or destroy
                    if ((state & ~(TASK_REFERENCE - 1)) == 0) {
                        if (state & TASK_CLOSED) {
                            this->m_vtable->destroy(this);
                        } else {
                            this->schedule();
                        }
                    }

                    break;
                }
            }
        }

        return out;
    }

    void abort() {
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
};

template <Pollable P>
struct Task : TaskTypedBase<typename P::Output> {
    using Output = TaskTypedBase<typename P::Output>::Output;
    using NonVoidOutput = TaskTypedBase<typename P::Output>::NVOutput;
    static constexpr bool IsVoid = std::is_void_v<Output>;

    ManuallyDrop<P> m_future;

    static Task* create(Runtime* runtime, P&& fut) {
        auto task = new Task{
            {{
                .m_runtime = runtime,
                .m_vtable = &vtable,
            }},
            std::move(fut),
        };
        return task;
    }

    static void vDropFuture(void* ptr) {
        TRACE("[Task {}] dropping future", ptr);
        auto self = static_cast<Task*>(ptr);
        self->m_future.drop();
    }

    static void vDestroy(void* self) {
        TRACE("[Task {}] destroying", self);
        auto task = static_cast<Task*>(self);
        task->m_runtime->removeTask(task);
        delete task;
    }

    static RawWaker vCloneWaker(void* self) {
        TRACE("[Task {}] cloning waker", self);
        auto task = static_cast<Task*>(self);
        auto state = task->incref();

        if (state > (uint64_t)(std::numeric_limits<int64_t>::max())) {
            std::abort();
        }

        return RawWaker{self, &WakerVtable};
    }

    static bool vRun(void* ptr) {
        auto self = static_cast<Task*>(ptr);
        return self->run();
    }

    template <bool Consume>
    static void vWake(void* ptr) {
        TRACE("[Task {}] waking", ptr);

        auto self = static_cast<Task*>(ptr);
        auto state = self->getState();

        while (true) {
            if ((state & (TASK_COMPLETED | TASK_CLOSED)) != 0) {
                if constexpr (Consume) self->vDropWaker(self);
                break;
            }

            // if the task is already scheduled, synchronize with the thread that will run the task
            if (state & TASK_SCHEDULED) {
                if (self->exchangeState(state, state)) {
                    if constexpr (Consume) self->vDropWaker(self);
                    break;
                }
            } else {
                // if the task is not running, schedule it
                auto newState = state | TASK_SCHEDULED;

                if (!Consume && ((state & TASK_RUNNING) == 0)) {
                    newState += TASK_REFERENCE;
                }

                if (self->exchangeState(state, newState)) {
                    if (state & TASK_RUNNING) {
                        if constexpr (Consume) self->vDropWaker(self);
                    } else {
                        self->schedule();
                    }

                    break;
                }
            }
        }
    }

    static constexpr TaskVtable vtable = {
        .schedule = &Task::vSchedule,
        .dropFuture = &Task::vDropFuture,
        .dropRef = &Task::vDropRef,
        .destroy = &Task::vDestroy,
        .run = &Task::vRun,
        .cloneWaker = &Task::vCloneWaker,
    };

    static constexpr RawWakerVtable WakerVtable = {
        .wake = &Task::vWake<true>,
        .wakeByRef = &Task::vWake<false>,
        .clone = &Task::vCloneWaker,
        .destroy = &Task::vDropWaker,
    };

    bool run() {
        ManuallyDrop<Waker> waker{this, &WakerVtable};
        auto state = this->getState();

        TRACE("[Task {}] running, state: {}", (void*)this, state);

        // update task state
        while (true) {
            if (state & TASK_CLOSED) {
                // closed, drop the task
                this->vDropFuture(this);

                auto state = this->m_state.fetch_and(~TASK_SCHEDULED, std::memory_order::acq_rel);

                std::optional<Waker> awaiter;
                if (state & TASK_AWAITER) {
                    awaiter = this->takeAwaiter();
                }

                this->vDropRef(this);

                if (awaiter) {
                    awaiter->wake();
                }

                return false;
            }

            auto newState = (state & ~TASK_SCHEDULED) | TASK_RUNNING;
            if (this->exchangeState(state, newState)) {
                state = newState;
                break;
            }
        }

        // poll the inner future
        ctx().m_waker = &waker.get();

        auto result = m_future.get().vPoll();

        ctx().m_waker = nullptr;

        TRACE("[Task {}] future completion: {}", (void*)this, result);

        if (result) {
            if constexpr (!IsVoid) {
                this->m_value = m_future.get().template vGetOutput<Output>();
            }

            this->vDropFuture(this);

            // the task is completed, update state
            while (true) {
                auto newState = (state & ~TASK_RUNNING & ~TASK_SCHEDULED) | TASK_COMPLETED;
                if ((state & TASK_TASK) == 0) {
                    newState |= TASK_CLOSED;
                }

                if (this->exchangeState(state, newState)) {
                    // if the task handle is destroyed or was closed while running, drop output value
                    if ((state & TASK_TASK) == 0 || (state & TASK_CLOSED) != 0) {
                        this->m_value.reset();
                    }

                    // take out the awaiter
                    std::optional<Waker> awaiter;
                    if (state & TASK_AWAITER) {
                        awaiter = this->takeAwaiter();
                    }

                    this->vDropRef(this);

                    // notify awaiter
                    if (awaiter) {
                        awaiter->wake();
                    }
                    break;
                }
            }
        } else {
            // task is still pending
            bool dropped = false;

            while (true) {
                auto newState = (state & ~TASK_RUNNING);

                if (state & TASK_CLOSED) {
                    newState &= ~TASK_SCHEDULED;
                }

                if ((state & TASK_CLOSED) && !dropped) {
                    // the thread that closed the task did not drop the future,
                    // so we have to do it here
                    this->vDropFuture(this);
                    dropped = true;
                }

                if (this->exchangeState(state, newState)) {
                    // if the task was closed while running, notify the awaiter
                    if (state & TASK_CLOSED) {
                        std::optional<Waker> awaiter;
                        if (state & TASK_AWAITER) {
                            awaiter = this->takeAwaiter();
                        }

                        this->vDropRef(this);

                        if (awaiter) {
                            awaiter->wake();
                        }
                    } else if (state & TASK_SCHEDULED) {
                        // if the task was woken up while running, reschedule it
                        this->schedule();
                        return true;
                    } else {
                        // drop reference held by the running state
                        this->vDropRef(this);
                    }
                    break;
                }
            }
        }

        return false;
    }
};

template <typename T>
struct TaskHandle {
    using handle_type = std::coroutine_handle<Promise<T>>;
    TaskTypedBase<T>* m_task = nullptr;

    TaskHandle(TaskTypedBase<T>* task) : m_task(task) {}

    TaskHandle(const TaskHandle&) = delete;
    TaskHandle& operator=(const TaskHandle&) = delete;

    TaskHandle(TaskHandle&& other) noexcept : m_task(std::exchange(other.m_task, nullptr)) {}
    TaskHandle& operator=(TaskHandle&& other) noexcept {
        if (this != &other) {
            if (m_task) m_task->detach();
            m_task = std::exchange(other.m_task, nullptr);
        }
        return *this;
    }

    ~TaskHandle() {
        if (m_task) m_task->detach();
    }

    /// Polls the task. Returns the return value if the future is completed,
    /// or std::nullopt if it is still pending.
    /// Throws an exception if the task was closed before completion.
    std::optional<typename TaskTypedBase<T>::NVOutput> pollTask() {
        auto res = m_task->pollTask();

        if (res && *res) {
            if constexpr (!std::is_void_v<T>) {
                return std::move(m_task->m_value.value());
            } else {
                return std::monostate{};
            }
        } else if (res) {
            // task was closed, raise exception
            throw std::runtime_error("Task polled after being closed");
        } else {
            return std::nullopt;
        }
    }

    /// Blocks until the task is completed.
    /// Do not use this inside async code.
    typename TaskTypedBase<T>::Output blockOn() noexcept {
        CondvarWaker cvw;
        auto waker = cvw.waker();
        m_task->registerAwaiter(waker);

        while (true) {
            auto result = this->pollTask();
            if (result) {
                if constexpr (!std::is_void_v<T>) {
                    return std::move(*result);
                } else {
                    return;
                }
            }

            cvw.wait();
        }
    }

    void abort() noexcept {
        m_task->abort();
    }

    bool await_ready() noexcept {
        return false;
    }

    bool await_suspend(std::coroutine_handle<> awaiter) {
        auto res = m_task->pollTask();
        TRACE("[Task {}] poll result: {}", (void*)m_task, res);

        if (res && *res) {
            return false; // task completed, don't suspend
        } else if (res) {
            // task was closed, raise exception
            throw std::runtime_error("Task polled after being closed");
        } else {
            // still pending, suspend
            return true;
        }
    }

    T await_resume() noexcept {
        TRACE("[Task {}] poll resuming", (void*)m_task);
        if constexpr (!std::is_void_v<T>) {
            return std::move(m_task->m_value.value());
        }
    }
};

}