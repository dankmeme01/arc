#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <unordered_set>
#include <cstddef>
#include <condition_variable>
#include <arc/task/Task.hpp>
#include <arc/task/BlockingTask.hpp>
#include <arc/task/CondvarWaker.hpp>

#include <asp/time/Duration.hpp>
#include <asp/ptr/SharedPtr.hpp>
#include <asp/time/sleep.hpp>
#include <arc/util/Function.hpp>
#include "TimeDriver.hpp"
#include "SignalDriver.hpp"
#include "IoDriver.hpp"

namespace arc {

using TerminateHandler = arc::MoveOnlyFunction<void(const std::exception&)>;

enum class DriverType {
    Time,
    Io,
    Signal,
};

struct RuntimeVtable {
    using EnqueueTaskFn = void(*)(Runtime*, TaskBase*);
    using SetTerminateHandlerFn = void(*)(Runtime*, TerminateHandler) noexcept;
    using InsertTaskFn = void(*)(Runtime*, TaskBase*);
    using InsertBlockingFn = void(*)(Runtime*, asp::SharedPtr<BlockingTaskBase>);
    using RemoveTaskFn = void(*)(Runtime*, TaskBase*) noexcept;
    using IsShuttingDownFn = bool(*)(const Runtime*) noexcept;
    using SafeShutdownFn = void(*)(Runtime*);
    using GetDriverFn = void*(*)(Runtime*, DriverType) noexcept;

    EnqueueTaskFn m_enqueueTask = nullptr;
    SetTerminateHandlerFn m_setTerminateHandler = nullptr;
    InsertTaskFn m_insertTask = nullptr;
    InsertBlockingFn m_insertBlocking = nullptr;
    RemoveTaskFn m_removeTask = nullptr;
    IsShuttingDownFn m_isShuttingDown = nullptr;
    SafeShutdownFn m_safeShutdown = nullptr;
    GetDriverFn m_getDriver = nullptr;
};

class Runtime : public asp::EnableSharedFromThis<Runtime> {
private:
    struct ctor_tag {};

public:
    // Creates a runtime
    static asp::SharedPtr<Runtime> create(
        size_t workers = std::thread::hardware_concurrency(),
        bool timeDriver = true,
        bool ioDriver = true,
        bool signalDriver = true
    );

    // Internal constructor, use `create` instead
    Runtime(ctor_tag, size_t workers);
    ~Runtime();

    /// Get the thread-local runtime context, returns nullptr if not inside a runtime.
    static Runtime* current();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    auto& timeDriver() { return getDriver<TimeDriver>(DriverType::Time); }
    auto& signalDriver() { return getDriver<SignalDriver>(DriverType::Signal); }
    auto& ioDriver() { return getDriver<IoDriver>(DriverType::Io); }

    /// Set the function that is called when an uncaught exception causes runtime termination.
    /// By default, this function just rethrows the exception to call std::terminate().
    void setTerminateHandler(TerminateHandler handler);

    void enqueueTask(TaskBase* task);

    template <IsPollable F, typename T = typename F::Output>
    TaskHandle<T> spawn(F fut) {
        auto* task = Task<F>::create(weakFromThis(), std::move(fut));
        m_vtable->m_insertTask(this, task);
        task->schedule();

        return TaskHandle<T>{task};
    }

    template <typename T>
    BlockingTaskHandle<T> spawnBlocking(arc::MoveOnlyFunction<T()> func) {
        BlockingTaskHandle<T> handle{ BlockingTask<T>::create(weakFromThis(), std::move(func)) };
        m_vtable->m_insertBlocking(this, handle.m_task);
        return handle;
    }

    template <IsPollable F, typename T = typename F::Output>
    T blockOn(F fut) {
        auto handle = this->spawn(std::move(fut));
        return handle.blockOn();
    }

    bool isShuttingDown() const noexcept;

    /// Safely shuts down the runtime and blocks until it is complete.
    /// A runtime can often be shutdown by simply dropping all references to it,
    /// but this method ensures all tasks and worker threads are immediately destroyed before returning.
    void safeShutdown();

private:
    template <IsPollable P>
    friend struct Task;

    struct WorkerData {
        std::thread thread;
        size_t id;
    };

    const RuntimeVtable* m_vtable;

    size_t m_workerCount;
    std::atomic<bool> m_stopFlag{false};
    std::optional<TimeDriver> m_timeDriver;
    std::optional<SignalDriver> m_signalDriver;
    std::optional<IoDriver> m_ioDriver;
    asp::SpinLock<TerminateHandler> m_terminateHandler;

    std::mutex m_mtx;
    asp::SpinLock<std::unordered_set<TaskBase*>> m_tasks;
    std::deque<TaskBase*> m_runQueue; // protected by m_mtx
    std::vector<WorkerData> m_workers;
    std::condition_variable m_cv;
    asp::time::Duration m_taskDeadline;

    std::mutex m_blockingMtx;
    std::deque<asp::SharedPtr<BlockingTaskBase>> m_blockingTasks; // protected by m_blockingMtx
    std::condition_variable m_blockingCv;
    std::atomic<size_t> m_blockingWorkers{0};
    std::atomic<size_t> m_freeBlockingWorkers{0};
    std::atomic<size_t> m_nextBlockingWorkerId{0};

    template <typename T>
    T& getDriver(DriverType ty) {
        auto ptr = static_cast<T*>(m_vtable->m_getDriver(this, ty));
        ARC_ASSERT(ptr, "attempted to access driver that is not available");
        return *ptr;
    }

    void init(bool timeDriver, bool ioDriver, bool signalDriver);
    void shutdown();

    void workerLoop(WorkerData& data, Context& cx);
    void workerLoopWrapper(WorkerData& data);
    void blockingWorkerLoop(size_t id);

    void removeTask(TaskBase* task) noexcept;
    void ensureBlockingWorker(size_t tasksInQueue);
    void spawnBlockingWorker();

    static void vSetTerminateHandler(Runtime* self, TerminateHandler handler) noexcept;
    static void vEnqueueTask(Runtime* self, TaskBase* task);
    static void vInsertTask(Runtime* self, TaskBase* task);
    static void vInsertBlocking(Runtime* self, asp::SharedPtr<BlockingTaskBase> task);
    static void vRemoveTask(Runtime* self, TaskBase* task) noexcept;
    static bool vIsShuttingDown(const Runtime* self) noexcept;
    static void vSafeShutdown(Runtime* self);
    static void* vGetDriver(Runtime* self, DriverType ty) noexcept;
};

/// Sets the global runtime that will be returned from `Runtime::current()`.
/// This is not thread-safe and not recommended to use in applications.
/// It is used in cases when multiple libraries statically link arc, but share a single runtime.
/// The library that owns the runtime will automatically have `Runtime::current()` work well,
/// but other libraries won't. They must obtain the runtime pointer and call this function.
///
/// Notably, this will also lead to `Runtime::current()` being non-null not inside task context.
void setGlobalRuntime(Runtime* rt);

/// Spawns an asynchronous task on the current runtime.
/// The returned handle can be awaited to get the result of the task, or discarded to run it in the background.
/// If there is no current runtime, an exception is thrown.
template <IsPollable F>
auto spawn(F t) {
    if (auto rt = Runtime::current()) {
        return rt->spawn(std::move(t));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

/// Spawns a blocking function onto a thread pool.
/// Use this when you need to run expensive synchronous code inside an async context.
/// The returned handle can be awaited to get the result of the function.
template <typename T>
auto spawnBlocking(arc::MoveOnlyFunction<T()> f) {
    if (auto rt = Runtime::current()) {
        return rt->spawnBlocking<T>(std::move(f));
    } else {
        throw std::runtime_error("No runtime available");
    }
}

}
