#pragma once

#include "Waker.hpp"
#include "CondvarWaker.hpp"
#include <arc/future/Promise.hpp>
#include <arc/future/Future.hpp>
#include <arc/util/Trace.hpp>
#include <arc/util/ManuallyDrop.hpp>
#include <arc/util/ScopeDtor.hpp>
#include <arc/util/Assert.hpp>

#if 0
# define TRACE trace
#else
# define TRACE(...) do {} while(0)
#endif

#include <asp/ptr/SharedPtr.hpp>
#include <asp/sync/SpinLock.hpp>
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

struct TaskDebugData {
    size_t m_debugDataSize{sizeof(TaskDebugData)};
    std::atomic<TaskBase*> m_task = nullptr;
    std::atomic<uint64_t> m_polls{0};
    std::atomic<uint64_t> m_runtimeNs{0};
    asp::SpinLock<std::string> m_name; // present already in task but duplicated here safety
    std::vector<void*> m_creationStack;

    asp::Duration totalRuntime() const noexcept;
    uint64_t totalPolls() const noexcept;
    std::string name() const noexcept;
    std::vector<void*> creationStack() const noexcept;
};

struct TaskVtable {
    using Fn = void(*)(void*);
    using AbortFn = void(*)(void*, bool);
    using RunFn = bool(*)(void*, Context&);
    using PollFn = std::optional<bool>(*)(void*, Context&);
    using CloneWakerFn = RawWaker(*)(void*);
    using RegisterAwaiterFn = void(*)(void*, Waker&);
    using NotifyAwaiterFn = void(*)(void*, Waker*);
    using TakeAwaiterFn = std::optional<Waker>(*)(void*, const Waker*);
    using SetNameFn = void(*)(void*, std::string);
    using GetNameFn = std::string_view(*)(void*);
    using GetDebugDataFn = asp::SharedPtr<TaskDebugData>(*)(void*);

    Fn schedule;
    Fn dropFuture;
    Fn dropRef;
    Fn destroy;
    AbortFn abort;
    RunFn run;
    PollFn poll;
    CloneWakerFn cloneWaker;
    RegisterAwaiterFn registerAwaiter;
    NotifyAwaiterFn notifyAwaiter;
    TakeAwaiterFn takeAwaiter;
    SetNameFn setName;
    GetNameFn getName;
    GetDebugDataFn getDebugData;
};

struct TaskBase {
    void schedule() noexcept;
    void abort() noexcept;
    void setName(std::string name) noexcept;
    asp::SharedPtr<TaskDebugData> getDebugData() noexcept;

    /// Polls the task. Returns:
    /// - std::nullopt if the task is pending
    /// - true if the task is completed
    /// - false if the task was closed before completion
    static std::optional<bool> vPoll(void* self, Context& cx);

    // Every field past the vtable can be changed without causing an ABI break.
    // Fields must never be directly accessed and should only be used through the vtable.
    const TaskVtable* m_vtable;

protected:
    friend class Runtime;
    template <typename T>
    friend struct TaskHandleBase;

    TaskBase(const TaskVtable* vtable, asp::WeakPtr<Runtime> runtime)
        : m_vtable(vtable), m_runtime(std::move(runtime)) {}

    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    std::atomic<uint64_t> m_state{TASK_SCHEDULED | TASK_REFERENCE | TASK_TASK};
    asp::WeakPtr<Runtime> m_runtime;
    std::optional<Waker> m_awaiter;
    std::string m_name;
    asp::SharedPtr<TaskDebugData> m_debugData;

    static bool shouldDestroy(uint64_t state) noexcept;
    uint64_t incref() noexcept;
    uint64_t decref() noexcept;
    void setState(uint64_t newState) noexcept;
    uint64_t getState() noexcept;
    bool exchangeState(uint64_t& expected, uint64_t newState) noexcept;
    void ensureDebugData();

    void registerAwaiter(Waker& waker);
    void notifyAwaiter(Waker* current = nullptr);
    std::optional<Waker> takeAwaiter(const Waker* current = nullptr);

    static void vAbort(void* self, bool force) noexcept;
    static void vSchedule(void* self);
    static void vDropRef(void* self);
    static void vDropWaker(void* ptr);
    static void vRegisterAwaiter(void* ptr, Waker& waker);
    static void vNotifyAwaiter(void* ptr, Waker* current);
    static std::optional<Waker> vTakeAwaiter(void* ptr, const Waker* current);
    static void vSetName(void* ptr, std::string name);
    static std::string_view vGetName(void* ptr);
    static asp::SharedPtr<TaskDebugData> vGetDebugData(void* ptr);
};

template <typename T>
struct TaskTypedBase : TaskBase {
    using Output = T;
    static constexpr bool IsVoid = std::is_void_v<Output>;
    using NVOutput = std::conditional_t<IsVoid, std::monostate, Output>;
    using TaskBase::TaskBase;

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
};

template <IsPollable P>
struct Task : TaskTypedBase<typename P::Output> {
    using Output = TaskTypedBase<typename P::Output>::Output;
    using NonVoidOutput = TaskTypedBase<typename P::Output>::NVOutput;
    static constexpr bool IsVoid = std::is_void_v<Output>;

protected:
    ManuallyDrop<P> m_future;
    std::atomic<bool> m_droppedFuture = false;

    Task(const TaskVtable* vtable, asp::WeakPtr<Runtime> runtime, P&& fut)
        : TaskTypedBase<typename P::Output>(vtable, std::move(runtime)), m_future(std::move(fut)) {}

public:
    static Task* create(asp::WeakPtr<Runtime> runtime, P&& fut) {
        auto task = new Task(&Task::vtable, std::move(runtime), std::move(fut));
#ifdef ARC_DEBUG
        task->ensureDebugData();
#endif
        return task;
    }

    ~Task() {
        if (this->m_debugData) {
            this->m_debugData->m_task.store(nullptr, std::memory_order::release);
        }

        if (!m_droppedFuture.load(std::memory_order::acquire)) {
            this->vDropFuture(this);
        }
    }

    static void vDropFuture(void* ptr) {
        TRACE("[Task {}] dropping future", ptr);
        auto self = static_cast<Task*>(ptr);
        ARC_DEBUG_ASSERT(!self->m_droppedFuture.exchange(true, std::memory_order::acq_rel));

        self->m_future.drop();
    }

    static void vDestroy(void* self) {
        TRACE("[Task {}] destroying", self);
        auto task = static_cast<Task*>(self);
        auto rt = task->m_runtime.upgrade();
        if (rt) {
            rt->removeTask(task);
        }
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

    static bool vRun(void* ptr, Context& cx) {
        auto self = static_cast<Task*>(ptr);
        return self->run(cx);
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
        .abort = &Task::vAbort,
        .run = &Task::vRun,
        .poll = &Task::vPoll,
        .cloneWaker = &Task::vCloneWaker,
        .registerAwaiter = &Task::vRegisterAwaiter,
        .notifyAwaiter = &Task::vNotifyAwaiter,
        .takeAwaiter = &Task::vTakeAwaiter,
        .setName = &Task::vSetName,
        .getName = &Task::vGetName,
        .getDebugData = &Task::vGetDebugData,
    };

    static constexpr RawWakerVtable WakerVtable = {
        .wake = &Task::vWake<true>,
        .wakeByRef = &Task::vWake<false>,
        .clone = &Task::vCloneWaker,
        .destroy = &Task::vDropWaker,
    };

    bool run(Context& cx) {
        ManuallyDrop<Waker> waker{this, &WakerVtable};
        auto state = this->getState();

        TRACE("[Task {}] polled, state: {}", (void*)this, state);

#ifdef ARC_DEBUG
        this->ensureDebugData();
        this->m_debugData->m_polls.fetch_add(1, std::memory_order::relaxed);
#endif

        // auto rt = this->m_runtime.upgrade();
        // if (!rt) return false; // might happen if the runtime is shutting down

        // update task state
        while (true) {
            if (state & TASK_CLOSED) {
                // closed, drop the future
                if (!m_droppedFuture.load(std::memory_order::acquire)) {
                    this->vDropFuture(this);
                }

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

#ifdef ARC_DEBUG
        auto startTime = asp::Instant::now();
#endif

        PollableBase* future = &m_future.get();
        cx._installWaker(&waker.get());
        bool result = future->m_vtable->m_poll(future, cx);
        cx._installWaker(nullptr);

#ifdef ARC_DEBUG
        uint64_t taken = startTime.elapsed().nanos();
        this->m_debugData->m_runtimeNs.fetch_add(taken, std::memory_order::relaxed);
#endif

        TRACE("[Task {}] future completion: {}", (void*)this, result);

        if (result) {
            if constexpr (!IsVoid) {
                this->m_value = future->m_vtable->getOutput<NonVoidOutput>(future, cx);
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
            while (true) {
                auto newState = (state & ~TASK_RUNNING);

                if (state & TASK_CLOSED) {
                    newState &= ~TASK_SCHEDULED;
                }

                if ((state & TASK_CLOSED) && !m_droppedFuture.load(std::memory_order::acquire)) {
                    // the thread that closed the task did not drop the future,
                    // so we have to do it here
                    this->vDropFuture(this);
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
struct TaskHandleBase {
    TaskTypedBase<T>* m_task = nullptr;

    TaskHandleBase(TaskTypedBase<T>* task) : m_task(task) {}

    TaskHandleBase(const TaskHandleBase&) = delete;
    TaskHandleBase& operator=(const TaskHandleBase&) = delete;

    TaskHandleBase(TaskHandleBase&& other) noexcept : m_task(std::exchange(other.m_task, nullptr)) {}
    TaskHandleBase& operator=(TaskHandleBase&& other) noexcept {
        if (this != &other) {
            if (m_task) m_task->detach();
            m_task = std::exchange(other.m_task, nullptr);
        }
        return *this;
    }

    ~TaskHandleBase() {
        if (m_task) m_task->detach();
    }

    /// Polls the task. Returns the return value if the future is completed,
    /// or std::nullopt if it is still pending.
    /// Throws an exception if the task was closed before completion.
    std::optional<typename TaskTypedBase<T>::NVOutput> pollTask(Context& cx) {
        auto res = m_task->vPoll(m_task, cx);
        TRACE("[Task {}] poll result: {}", (void*)this->m_task, res);

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

        Context cx { &waker, nullptr };

        while (true) {
            auto result = this->pollTask(cx);
            TRACE("[Task {}] poll result: {}", (void*)this->m_task, result);

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

    void setName(std::string name) noexcept {
        m_task->setName(std::move(name));
    }

    asp::SharedPtr<TaskDebugData> getDebugData() noexcept {
        return m_task->getDebugData();
    }
};

template <typename T>
struct TaskHandle : Pollable<TaskHandle<T>, T>, TaskHandleBase<T> {
    TaskHandle(TaskTypedBase<T>* task) : TaskHandleBase<T>(task) {}

    std::optional<T> poll(Context& cx) {
        return this->pollTask(cx);
    }
};

template <>
struct TaskHandle<void> : Pollable<TaskHandle<void>, void>, TaskHandleBase<void> {
    TaskHandle(TaskTypedBase<void>* task) : TaskHandleBase<void>(task) {}

    bool poll(Context& cx) {
        auto res = TaskHandleBase::pollTask(cx);
        return res.has_value();
    }
};

}

#undef TRACE
