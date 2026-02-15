#pragma once

#include "Waker.hpp"
#include "CondvarWaker.hpp"
#include <arc/future/Promise.hpp>
#include <arc/future/Future.hpp>
#include <arc/util/Trace.hpp>
#include <arc/util/ManuallyDrop.hpp>
#include <arc/util/ScopeDtor.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Config.hpp>

#include <asp/ptr/SharedPtr.hpp>
#include <asp/sync/SpinLock.hpp>
#include <utility>
#include <atomic>
#include <limits>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace arc {

static constexpr uint64_t TASK_SCHEDULED   = 1 << 0; // scheduled to run again asap
static constexpr uint64_t TASK_RUNNING     = 1 << 1; // currently running
static constexpr uint64_t TASK_COMPLETED   = 1 << 2; // has completed, successfully or with an exception
static constexpr uint64_t TASK_CLOSED      = 1 << 3; // closed, return value will be unavailable
static constexpr uint64_t TASK_AWAITER     = 1 << 4; // has an awaiter
static constexpr uint64_t TASK_NOTIFYING   = 1 << 5; // currently taking the awaiter to notify it
static constexpr uint64_t TASK_REGISTERING = 1 << 6; // currently registering the awaiter
static constexpr uint64_t TASK_HANDLE      = 1 << 7; // presence of an active TaskHandle
static constexpr uint64_t TASK_ABANDONED   = 1 << 8; // no longer owned by a runtime
static constexpr uint64_t TASK_REFERENCE   = 1 << 12; // single reference

static constexpr uint64_t TASK_INITIAL_STATE = TASK_SCHEDULED | TASK_REFERENCE | TASK_HANDLE;

struct TaskDebugData {
    size_t m_debugDataSize{sizeof(TaskDebugData)};
    std::atomic<TaskBase*> m_task = nullptr;
    std::atomic<uint64_t> m_polls{0};
    std::atomic<uint64_t> m_runtimeNs{0};
    asp::SpinLock<asp::BoxedString> m_name; // present already in task but duplicated here safety
    std::vector<void*> m_creationStack;

    asp::Duration totalRuntime() const noexcept;
    uint64_t totalPolls() const noexcept;
    asp::BoxedString name() const noexcept;
    std::vector<void*> creationStack() const noexcept;
};

struct TaskVtable {
    using Fn = void(*)(void*);
    using AbortFn = void(*)(void*, bool);
    using RunFn = bool(*)(void*, Context&);
    using DetachFn = bool(*)(void*, void* out);
    using PollFn = std::optional<bool>(*)(void*, Context&);
    using GetOutputFn = void(*)(void*, void* out);
    using CloneWakerFn = RawWaker(*)(void*);
    using SetNameFn = void(*)(void*, asp::BoxedString);
    using GetNameFn = asp::BoxedString(*)(void*);
    using GetDebugDataFn = asp::SharedPtr<TaskDebugData>(*)(void*);

    Fn schedule;
    Fn destroy;
    AbortFn abort;
    RunFn run;
    DetachFn detach;
    PollFn poll;
    GetOutputFn getOutput;
    CloneWakerFn cloneWaker;
    SetNameFn setName;
    GetNameFn getName;
    GetDebugDataFn getDebugData;
};

struct TaskBase {
    void abort() noexcept;
    void setName(asp::BoxedString name) noexcept;
    asp::SharedPtr<TaskDebugData> getDebugData() noexcept;

    // Every field past the vtable can be changed without causing an ABI break.
    // Fields must never be directly accessed and should only be used through the vtable.
    const TaskVtable* m_vtable;

protected:
    friend class Runtime;
    template <typename T>
    friend struct TaskHandleBase;

    TaskBase(const TaskVtable* vtable, Runtime* runtime) noexcept
        : m_vtable(vtable), m_runtime(runtime) {}

    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    std::atomic<uint64_t> m_state{TASK_INITIAL_STATE};
    Runtime* m_runtime;
    Waker m_awaiter;
    asp::BoxedString m_name;
    asp::SharedPtr<TaskDebugData> m_debugData;
    std::exception_ptr m_exception;

    static bool shouldDestroy(uint64_t state) noexcept;
    uint64_t incref() noexcept;
    uint64_t decref() noexcept;
    size_t refcount() const noexcept;
    void setState(uint64_t newState) noexcept;
    uint64_t getState() noexcept;
    bool exchangeState(uint64_t& expected, uint64_t newState) noexcept;
    void ensureDebugData();
    std::string debugName();

    void registerAwaiter(Waker& waker);
    void notifyAwaiter(Waker* current = nullptr);
    Waker takeAwaiter(const Waker* current = nullptr);
    void dropRef();
    void schedule();

    static void vAbort(void* self, bool force);
    static void vSchedule(void* self);
    static void vDropWaker(void* ptr);
    static void vSetName(void* ptr, asp::BoxedString name) noexcept;
    static asp::BoxedString vGetName(void* ptr) noexcept;
    static asp::SharedPtr<TaskDebugData> vGetDebugData(void* ptr);

    /// Polls the task. Returns:
    /// - std::nullopt if the task is pending
    /// - true if the task is completed
    /// - false if the task was closed before completion
    static std::optional<bool> vPoll(void* self, Context& cx);
};

template <typename T>
struct TaskTypedBase : TaskBase {
    using Output = T;
    static constexpr bool IsVoid = std::is_void_v<Output>;
    using NVOutput = std::conditional_t<IsVoid, std::monostate, Output>;
    using TaskBase::TaskBase;

    std::optional<NVOutput> detach() {
        MaybeUninit<NVOutput> out;
        bool grabbed = m_vtable->detach(this, &out);
        return grabbed ? std::optional{std::move(out).assumeInit()} : std::nullopt;
    }

protected:
    std::optional<NVOutput> m_value;

    static bool vDetach(void* ptr, void* outp) {
        auto self = static_cast<TaskTypedBase*>(ptr);
        auto out = static_cast<MaybeUninit<NVOutput>*>(outp);

        auto state = self->getState();

        // commonly, the task is being detached right after being created, assume this may be the case
        auto expected = TASK_INITIAL_STATE;
        if (self->exchangeState(expected, TASK_SCHEDULED | TASK_REFERENCE)) {
            return false;
        }

        bool grabbedOutput = false;

        // didn't guess, will have to loop
        while (true) {
            if (state & TASK_COMPLETED && (state & TASK_CLOSED) == 0) {
                // mark as closed, grab the output
                if (self->exchangeState(state, state | TASK_CLOSED)) {
                    if (out && self->m_value) out->init(std::move(*self->m_value));
                    state |= TASK_CLOSED;
                    grabbedOutput = true;
                }
            } else {
                // if this is the last reference and the task isn't closed, close and schedule again
                auto newState = state & ~TASK_HANDLE;
                if ((state & (~(TASK_REFERENCE - 1) | TASK_CLOSED)) == 0) {
                    newState = TASK_SCHEDULED | TASK_CLOSED | TASK_REFERENCE;
                }

                if (self->exchangeState(state, newState)) {
                    // if this is the last reference, either schedule or destroy
                    if ((state & ~(TASK_REFERENCE - 1)) == 0) {
                        if (state & TASK_CLOSED) {
                            self->m_vtable->destroy(self);
                        } else {
                            self->schedule();
                        }
                    }

                    break;
                }
            }
        }

        return grabbedOutput;
    }
};

template <IsPollable Fut, typename Lambda = std::monostate>
struct Task : TaskTypedBase<typename Fut::Output> {
    using Output = TaskTypedBase<typename Fut::Output>::Output;
    using NonVoidOutput = TaskTypedBase<typename Fut::Output>::NVOutput;
    static constexpr bool UsesLambda = !std::is_same_v<Lambda, std::monostate>;
    static constexpr bool IsVoid = std::is_void_v<Output>;

protected:
#if defined(__clang__) || defined(__GNUC__)
    ARC_NO_UNIQUE_ADDRESS
#endif
    std::conditional_t<UsesLambda, ManuallyDrop<Lambda>, std::monostate> m_lambda;

    ManuallyDrop<Fut> m_future;
    std::atomic<bool> m_droppedFuture = false;

    template <typename F>
    Task(const TaskVtable* vtable, Runtime* runtime, F&& fut) requires (!UsesLambda)
        : TaskTypedBase<typename Fut::Output>(vtable, runtime), m_future(std::forward<F>(fut)) {}

    template <typename L>
    Task(const TaskVtable* vtable, Runtime* runtime, L&& fut) requires (UsesLambda)
        : TaskTypedBase<typename Fut::Output>(vtable, runtime),
          m_lambda(std::forward<L>(fut)),
          m_future(std::invoke(m_lambda.get())) {}

public:
    template <typename FutOrLambda>
    static Task* create(Runtime* runtime, FutOrLambda&& fut) {
        auto task = new Task(&Task::vtable, runtime, std::forward<FutOrLambda>(fut));
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
            this->dropFuture();
        }
    }

protected:
    void dropFuture() {
        if (this->m_droppedFuture.exchange(true, std::memory_order::acq_rel)) return;

        ARC_TRACE("[{}] dropping future (has lambda: {})", this->debugName(), UsesLambda);

        this->m_future.drop();

        if constexpr (UsesLambda) {
            this->m_lambda.drop();
        }
    }

    static void vDestroy(void* self) {
        auto task = static_cast<Task*>(self);
        ARC_TRACE("[{}] destroying", task->debugName());

        // remove the task from the runtime if we haven't been abandoned
        auto state = task->getState();
        if ((state & TASK_ABANDONED) == 0) {
            task->m_runtime->removeTask(task);
        }

        delete task;
    }

    static RawWaker vCloneWaker(void* self) {
        auto task = static_cast<Task*>(self);
        ARC_TRACE("[{}] cloning waker", task->debugName());
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
        auto self = static_cast<Task*>(ptr);
        ARC_TRACE("[{}] waking", self->debugName());
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

    /// Obtains the output of the task, rethrowing any potential exceptions.
    /// This should only be called after a vPoll returns true, and not more than once.
    static void vGetOutput(void* ptr, void* out) {
        auto self = static_cast<Task*>(ptr);
        if (self->m_exception) {
            ARC_TRACE("[{}] rethrowing exception from task", self->debugName());
            std::rethrow_exception(self->m_exception);
        }

        if constexpr (!IsVoid) {
            auto outp = reinterpret_cast<MaybeUninit<NonVoidOutput>*>(out);
            outp->init(std::move(self->m_value).value());
        }
    }

    static constexpr TaskVtable vtable = {
        .schedule = &Task::vSchedule,
        .destroy = &Task::vDestroy,
        .abort = &Task::vAbort,
        .run = &Task::vRun,
        .detach = &Task::vDetach,
        .poll = &Task::vPoll,
        .getOutput = &Task::vGetOutput,
        .cloneWaker = &Task::vCloneWaker,
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

        ARC_TRACE("[{}] polled, state: {}", this->debugName(), state);

#ifdef ARC_DEBUG
        this->ensureDebugData();
        this->m_debugData->m_polls.fetch_add(1, std::memory_order::relaxed);
#endif

        // update task state
        while (true) {
            if (state & TASK_CLOSED) {
                // closed, drop the future
                this->dropFuture();

                auto state = this->m_state.fetch_and(~TASK_SCHEDULED, std::memory_order::acq_rel);

                Waker awaiter;
                if (state & TASK_AWAITER) {
                    awaiter = this->takeAwaiter();
                }

                this->dropRef();

                if (awaiter) {
                    awaiter.wake();
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

        ARC_TRACE("[{}] future completion: {}", this->debugName(), result);

        if (result) {
            try {
                if constexpr (!IsVoid) {
                    this->m_value = future->m_vtable->getOutput<NonVoidOutput>(future);
                } else {
                    auto func = future->m_vtable->m_getOutput;
                    if (func) func(future, nullptr);
                }
            } catch (const std::exception& e) {
                this->m_exception = std::current_exception();

                printError("[{}] Task terminated due to exception: {}", this->debugName(), e.what());
                cx.dumpStack();
            }

            this->dropFuture();

            // the task is completed, update state
            while (true) {
                auto newState = (state & ~TASK_RUNNING & ~TASK_SCHEDULED) | TASK_COMPLETED;
                if ((state & TASK_HANDLE) == 0) {
                    newState |= TASK_CLOSED;
                }

                if (this->exchangeState(state, newState)) {
                    // if the task handle is destroyed or was closed while running, drop output value
                    if ((state & TASK_HANDLE) == 0 || (state & TASK_CLOSED) != 0) {
                        this->m_value.reset();
                    }

                    // take out the awaiter
                    Waker awaiter;
                    if (state & TASK_AWAITER) {
                        awaiter = this->takeAwaiter();
                    }

                    this->dropRef();

                    // notify awaiter
                    if (awaiter) {
                        awaiter.wake();
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

                if (state & TASK_CLOSED) {
                    // in case the future hasn't been dropped yet, do it now
                    this->dropFuture();
                }

                if (this->exchangeState(state, newState)) {
                    // if the task was closed while running, notify the awaiter
                    if (state & TASK_CLOSED) {
                        Waker awaiter;
                        if (state & TASK_AWAITER) {
                            awaiter = this->takeAwaiter();
                        }

                        this->dropRef();

                        if (awaiter) {
                            awaiter.wake();
                        }
                    } else if (state & TASK_SCHEDULED) {
                        // if the task was woken up while running, reschedule it
                        this->schedule();
                        return true;
                    } else {
                        // drop reference held by the running state
                        this->dropRef();
                    }
                    break;
                }
            }
        }

        return false;
    }
};

template <typename T = void>
struct TaskHandleBase {
    TaskTypedBase<T>* m_task = nullptr;

    TaskHandleBase() noexcept = default;
    TaskHandleBase(TaskTypedBase<T>* task) noexcept : m_task(task) {}

    TaskHandleBase(const TaskHandleBase&) = delete;
    TaskHandleBase& operator=(const TaskHandleBase&) = delete;

    TaskHandleBase(TaskHandleBase&& other) noexcept : m_task(std::exchange(other.m_task, nullptr)) {}
    TaskHandleBase& operator=(TaskHandleBase&& other) noexcept {
        if (this != &other) {
            this->detach();
            m_task = std::exchange(other.m_task, nullptr);
        }
        return *this;
    }

    ~TaskHandleBase() {
        this->detach();
    }

    /// Polls the task. Returns the return value if the future is completed,
    /// or std::nullopt if it is still pending.
    /// Throws an exception if the task was closed before completion or if the task threw.
    /// If the task is completed or threw, invalidates this handle.
    std::optional<typename TaskTypedBase<T>::NVOutput> pollTask(Context& cx) {
        this->validate();
        auto res = m_task->m_vtable->poll(m_task, cx);
        ARC_TRACE("[{}] poll result: {}", this->m_task->debugName(), res);

        if (res && *res) {
            auto _dtor = scopeDtor([this] {
                this->detach();
            });

            if constexpr (!std::is_void_v<T>) {
                MaybeUninit<typename TaskTypedBase<T>::NVOutput> out;
                m_task->m_vtable->getOutput(m_task, &out);
                return std::move(out.assumeInit());
            } else {
                // no return value, so just rethrow the exception, if any
                m_task->m_vtable->getOutput(m_task, nullptr);
                return std::monostate{};
            }
        } else if (res) {
            // task was closed, raise exception
            throw std::runtime_error("Task polled after being closed");
        } else {
            return std::nullopt;
        }
    }

    /// Blocks until the task is completed. Do not use this inside async code.
    /// Invalidates this handle, throws if the handle is already invalid.
    typename TaskTypedBase<T>::Output blockOn() {
        this->validate();
        CondvarWaker cvw;
        auto waker = cvw.waker();

        Context cx { &waker, nullptr };

        while (true) {
            auto result = this->pollTask(cx);
            ARC_TRACE("[{}] poll result: {}", (void*)this->m_task, (bool)result);

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

    /// Aborts the task, ensuring it will not complete and will not get scheduled again.
    /// If the task is currently being executed, it will still run until it yields.
    /// Invalidates this handle, throws if the handle is already invalid.
    void abort() {
        this->validate();
        m_task->abort();
        this->detach();
    }

    void setName(asp::BoxedString name) {
        this->validate();
        m_task->setName(std::move(name));
    }

    asp::SharedPtr<TaskDebugData> getDebugData() {
        this->validate();
        return m_task->getDebugData();
    }

    /// Checks if the handle is valid (i.e. it points to a task that hasn't been detached yet).
    bool isValid() const noexcept {
        return m_task != nullptr;
    }

    /// Detaches from the task, letting it discard the return value and automatically cleanup once finished.
    /// This is automatically called when the handle is destroyed. Does nothing if the handle is invalid.
    void detach() noexcept {
        if (m_task) {
            m_task->detach();
            m_task = nullptr;
        }
    }

    operator bool() const noexcept {
        return this->isValid();
    }

protected:
    void validate() const {
        if (!m_task) [[unlikely]] {
            throw std::runtime_error("Invalid task handle");
        }
    }
};

template <typename T = void>
struct TaskHandle : Pollable<TaskHandle<T>, T>, TaskHandleBase<T> {
    TaskHandle() noexcept = default;
    TaskHandle(TaskTypedBase<T>* task) noexcept : TaskHandleBase<T>(task) {}

    std::optional<T> poll(Context& cx) {
        return this->pollTask(cx);
    }
};

template <>
struct TaskHandle<void> : Pollable<TaskHandle<void>, void>, TaskHandleBase<void> {
    TaskHandle() noexcept = default;
    TaskHandle(TaskTypedBase<void>* task) noexcept : TaskHandleBase<void>(task) {}

    bool poll(Context& cx) {
        auto res = TaskHandleBase::pollTask(cx);
        return res.has_value();
    }
};

}

#undef TRACE
