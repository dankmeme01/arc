#include <arc/runtime/Runtime.hpp>
#include <asp/thread/Thread.hpp>
#include <asp/time/chrono.hpp>

using namespace asp::time;
using enum std::memory_order;

static constexpr size_t MAX_BLOCKING_WORKERS = 128;
static constexpr size_t MIN_BLOCKING_WORKERS = 2;
static thread_local arc::Runtime* g_runtime = nullptr;
static arc::Runtime* g_globalRuntime = nullptr;

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

asp::SharedPtr<Runtime> Runtime::create(const RuntimeOptions& options) {
    auto rt = asp::make_shared<Runtime>(ctor_tag{}, options.workers);
    rt->init(options);
    return rt;
}

asp::SharedPtr<Runtime> Runtime::create(size_t workers) {
    return create(RuntimeOptions{
        .workers = workers,
    });
}

Runtime::Runtime(ctor_tag, size_t workers)
    : m_stopFlag(false),
      m_workerCount(std::clamp<size_t>(workers, 1, 128)),
      m_taskDeadline(Duration::fromMillis((uint64_t)(5.f * std::powf(m_workerCount, 0.9f))))
{
    static constexpr RuntimeVtable vtable = {
        .m_enqueueTask = &Runtime::vEnqueueTask,
        .m_setTerminateHandler = &Runtime::vSetTerminateHandler,
        .m_insertTask = &Runtime::vInsertTask,
        .m_insertBlocking = &Runtime::vInsertBlocking,
        .m_removeTask = &Runtime::vRemoveTask,
        .m_isShuttingDown = &Runtime::vIsShuttingDown,
        .m_safeShutdown = &Runtime::vSafeShutdown,
        .m_getDriver = &Runtime::vGetDriver,
        .m_getTaskStats = &Runtime::vGetTaskStats,
    };

    m_vtable = &vtable;
}

void Runtime::init(const RuntimeOptions& options) {
    // most of the initialization is deferred until here,
    // because weak_from_this() does not work inside constructor

#ifdef ARC_FEATURE_TIME
    if (options.timeDriver) {
        m_timeDriver.emplace(weakFromThis());
    }
#endif
#ifdef ARC_FEATURE_NET
    if (options.ioDriver) {
        m_ioDriver.emplace(weakFromThis());
    }
#endif
#ifdef ARC_FEATURE_SIGNAL
    if (options.signalDriver) {
        m_signalDriver.emplace(weakFromThis());
    }
#endif
#ifdef ARC_FEATURE_IOCP
    if (options.iocpDriver) {
        m_iocpDriver.emplace(weakFromThis());
    }
#endif

    m_workers.reserve(m_workerCount);
    for (size_t i = 0; i < m_workerCount; ++i) {
        m_workers.emplace_back(WorkerData{
            .id = i,
        });
    }

    for (size_t i = 0; i < m_workerCount; ++i) {
        auto& worker = m_workers[i];
        worker.thread = std::thread([this, &worker] {
            asp::_setThreadName(fmt::format("arc-worker-{}", worker.id));

            this->workerLoopWrapper(worker);
        });
    }
}

Runtime::~Runtime() {
    this->shutdown();
}

Runtime* Runtime::current() {
    return g_runtime ? g_runtime : g_globalRuntime;
}

void Runtime::setTerminateHandler(TerminateHandler handler) {
    m_vtable->m_setTerminateHandler(this, std::move(handler));
}

void Runtime::enqueueTask(TaskBase* task) {
    m_vtable->m_enqueueTask(this, task);
}

void Runtime::removeTask(TaskBase* task) noexcept {
    m_vtable->m_removeTask(this, task);
}

void Runtime::vSetTerminateHandler(Runtime* self, TerminateHandler handler) noexcept {
    auto lock = self->m_terminateHandler.lock();
    lock = std::move(handler);
}

void Runtime::vEnqueueTask(Runtime* self, TaskBase* task) {
    TRACE("[Runtime] enqueuing task {}", (void*)task);
    {
        std::lock_guard lock(self->m_mtx);
        self->m_runQueue.push_back(task);
    }
    self->m_cv.notify_one();
}

void Runtime::vInsertTask(Runtime* self, TaskBase* task) {
    self->m_tasks.lock()->insert(task);
}

void Runtime::vInsertBlocking(Runtime* self, asp::SharedPtr<BlockingTaskBase> task) {
    std::unique_lock lock(self->m_blockingMtx);

    auto& btasks = self->m_blockingTasks;
    btasks.push_back(task);

    self->ensureBlockingWorker(btasks.size());
    self->m_blockingCv.notify_one();
}

static bool skipRemoveTask = false;
void Runtime::vRemoveTask(Runtime* self, TaskBase* task) noexcept {
    if (skipRemoveTask) return;
    self->m_tasks.lock()->erase(task);
}

bool Runtime::vIsShuttingDown(const Runtime* self) noexcept {
    return self->m_stopFlag.load(::acquire);
}

void Runtime::vSafeShutdown(Runtime* self) {
    self->shutdown();
}

void* Runtime::vGetDriver(Runtime* self, DriverType ty) noexcept {
    switch (ty) {
#ifdef ARC_FEATURE_TIME
        case DriverType::Time:
            return self->m_timeDriver ? &*self->m_timeDriver : nullptr;
#endif
#ifdef ARC_FEATURE_NET
        case DriverType::Io:
            return self->m_ioDriver ? &*self->m_ioDriver : nullptr;
#endif
#ifdef ARC_FEATURE_SIGNAL
        case DriverType::Signal:
            return self->m_signalDriver ? &*self->m_signalDriver : nullptr;
#endif
#ifdef ARC_FEATURE_IOCP
        case DriverType::Iocp:
            return self->m_iocpDriver ? &*self->m_iocpDriver : nullptr;
#endif
        default:
            return nullptr;
    }
}

void Runtime::vGetTaskStats(Runtime* self, std::vector<asp::SharedPtr<TaskDebugData>>& out) {
    auto tasks = self->m_tasks.lock();
    out.reserve(tasks->size());
    for (auto task : *tasks) {
        if (auto data = task->getDebugData()) {
            out.push_back(std::move(data));
        }
    }
}

void Runtime::workerLoopWrapper(WorkerData& data) {
    Context cx{nullptr, this};
    g_runtime = this;

    // Wrap around and catch exceptions to get better traces
    try {
        this->workerLoop(data, cx);
    } catch (const std::exception& e) {
        printError("[Worker {}] terminating due to uncaught exception: {}", data.id, e.what());
        cx.dumpStack();

        auto handler = m_terminateHandler.lock();
        if (*handler) {
            (*handler)(e);
        } else {
            throw; // rethrow
        }
    }
}

void Runtime::workerLoop(WorkerData& data, Context& cx) {
    float mult = std::powf(m_workers.size(), 0.9f);
    auto timerIncrement = Duration::fromMicros(500.f * mult);
    auto ioIncrement = Duration::fromMicros(800.f * mult);

    auto timerOffset = (timerIncrement * data.id) / m_workers.size();
    auto ioOffset = (ioIncrement * data.id) / m_workers.size();

    auto start = Instant::now();
    auto nextTimerTask = start + timerOffset;
    auto nextIoTask = start + ioOffset;
    uint64_t timerTick = 0;
    uint64_t ioTick = 0;

    while (!m_stopFlag.load(::acquire)) {
        auto now = Instant::now();
        auto deadline = now + Duration::fromHours(1); // arbitrary long deadline

        // every once in a while, run timer and io drivers
#ifdef ARC_FEATURE_TIME
        if (m_timeDriver && now >= nextTimerTask) {
            m_timeDriver->doWork();

            do {
                timerTick++;
                nextTimerTask = start + timerOffset + timerTick * timerIncrement;
            } while (now >= nextTimerTask);
        }
        if (m_timeDriver) {
            deadline = (std::min)(deadline, nextTimerTask);
        }
#endif

#if defined(ARC_FEATURE_NET) || defined(ARC_FEATURE_IOCP)
        bool hasIoDriver = false;
# if defined(ARC_FEATURE_NET)
        hasIoDriver = hasIoDriver || m_ioDriver.has_value();
# endif
# if defined(ARC_FEATURE_IOCP)
        hasIoDriver = hasIoDriver || m_iocpDriver.has_value();
# endif

        if (hasIoDriver) {
            if (now >= nextIoTask) {
#ifdef ARC_FEATURE_NET
                if (m_ioDriver) m_ioDriver->doWork();
#endif
#ifdef ARC_FEATURE_IOCP
                if (m_iocpDriver) m_iocpDriver->doWork();
#endif

                do {
                    ioTick++;
                    nextIoTask = start + ioOffset + ioTick * ioIncrement;
                } while (now >= nextIoTask);
            }

            deadline = (std::min)(deadline, nextIoTask);
        }
#endif

        now = Instant::now();
        auto wait = deadline.durationSince(now);

        TaskBase* task = nullptr;
        {
            std::unique_lock lock(m_mtx);
            bool success;

            if (wait.isZero()) {
                // don't wait
                success = m_stopFlag.load(::acquire) || !m_runQueue.empty();
            } else {
                success = m_cv.wait_for(lock, std::chrono::microseconds{wait.micros()}, [this] {
                    return m_stopFlag.load(::acquire) || !m_runQueue.empty();
                });
            }

            if (!success) {
                continue; // timeout
            }

            if (m_stopFlag.load(::acquire)) {
                break;
            }

            if (!m_runQueue.empty()) {
                task = m_runQueue.front();
                m_runQueue.pop_front();
            }
        }

        if (!task) {
            continue;
        }

        TRACE("[Worker {}] driving task {}", data.id, task->debugName());
        now = Instant::now();

        cx.setup(now + m_taskDeadline);
        task->m_vtable->run(task, cx);

        TRACE("[Worker {}] finished driving task {}", data.id, task->debugName());

#ifdef ARC_DEBUG
        auto taken = now.elapsed();
        if (taken > Duration::fromMillis(100)) {
            printWarn("[Worker {}] task {} took {} to yield", data.id, task->debugName(), taken.toString());
        }
#endif
    }
}

void Runtime::blockingWorkerLoop(size_t id) {
    constexpr auto IDLE_TIMEOUT = Duration::fromSecs(30);
    auto terminateAt = Instant::now() + IDLE_TIMEOUT;

    bool running = true;

    while (running) {
        auto now = Instant::now();

        // terminate if there has been no task in a while, and too many blocking workers already
        if (now >= terminateAt) {
            auto workers = m_blockingWorkers.load(::acquire);

            while (true) {
                if (workers <= MIN_BLOCKING_WORKERS) {
                    break; // do not terminate if we are at the minimum
                }

                if (m_blockingWorkers.compare_exchange_weak(workers, workers - 1, ::acq_rel, ::acquire)) {
                    running = false;
                    break;
                }
            }

            if (!running) break;
        }

        asp::SharedPtr<BlockingTaskBase> task = nullptr;
        {
            std::unique_lock lock(m_blockingMtx);

            bool success = m_blockingCv.wait_for(lock, asp::toChrono(IDLE_TIMEOUT), [this] {
                return m_stopFlag.load(::acquire) || !m_blockingTasks.empty();
            });

            if (!success) {
                continue; // timeout
            }

            if (m_stopFlag.load(::acquire)) {
                break;
            }

            auto& btasks = m_blockingTasks;
            if (!btasks.empty()) {
                task = std::move(btasks.front());
                btasks.pop_front();
            }
        }

        if (!task) {
            continue;
        }

        TRACE("[Blocking {}] executing blocking task {}", id, (void*)task);
        m_busyBlockingWorkers.fetch_add(1, ::relaxed);
        task->execute();
        m_busyBlockingWorkers.fetch_sub(1, ::relaxed);
        TRACE("[Blocking {}] finished blocking task {}", id, (void*)task);

        terminateAt = Instant::now() + IDLE_TIMEOUT;
    }

    TRACE("[Blocking {}] exiting due to inactivity", id);
}

void Runtime::ensureBlockingWorker(size_t tasksInQueue) {
    auto workers = m_blockingWorkers.load(::relaxed);

    if (workers >= MAX_BLOCKING_WORKERS) {
        return; // do not spawn more than that
    }

    // spawn a new worker if there are no free workers and there are tasks waiting
    if (tasksInQueue > 0 && m_busyBlockingWorkers.load(::relaxed) >= workers) {
        this->spawnBlockingWorker();
    }
}

void Runtime::spawnBlockingWorker() {
    auto num = m_blockingWorkers.fetch_add(1, ::relaxed) + 1;
    if (num > MAX_BLOCKING_WORKERS) {
        m_blockingWorkers.fetch_sub(1, ::relaxed);
        return; // do not spawn more than that
    }

    size_t workerId = m_nextBlockingWorkerId.fetch_add(1, ::relaxed);

    std::thread th([this, workerId] {
        asp::_setThreadName(fmt::format("arc-blocking-{}", workerId));
        this->blockingWorkerLoop(workerId);
    });
    th.detach();
}

bool Runtime::isShuttingDown() const noexcept {
    return m_vtable->m_isShuttingDown(this);
}

void Runtime::safeShutdown() {
    m_vtable->m_safeShutdown(this);
}

std::vector<asp::SharedPtr<TaskDebugData>> Runtime::getTaskStats() {
    std::vector<asp::SharedPtr<TaskDebugData>> out;
    m_vtable->m_getTaskStats(this, out);
    return out;
}

void Runtime::shutdown() {
    if (m_stopFlag.exchange(true, ::acq_rel)) {
        return;
    }

    TRACE("[Runtime] shutting down");
    m_cv.notify_all();
    m_blockingCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    m_workers.clear();
    m_blockingTasks.clear();

    // free all drivers
#ifdef ARC_FEATURE_TIME
    m_timeDriver.reset();
#endif
#ifdef ARC_FEATURE_NET
    m_ioDriver.reset();
#endif
#ifdef ARC_FEATURE_SIGNAL
    m_signalDriver.reset();
#endif
#ifdef ARC_FEATURE_IOCP
    m_iocpDriver.reset();
#endif

    // abort all tasks
    // it's not safe to call destroy on them because someone still might have TaskHandles
    // simply call abort and then run, so that stuff gets cleaned up

    Context cx { nullptr };
    skipRemoveTask = true;

    auto tasks = m_tasks.lock();
    for (auto* task : *tasks) {
        task->m_vtable->abort(task, true);
        task->m_vtable->run(task, cx);
    }
    tasks->clear();
    skipRemoveTask = false;
}

void setGlobalRuntime(Runtime* rt) {
    g_globalRuntime = rt;
}


}