#include <arc/runtime/Runtime.hpp>
#include <asp/thread/Thread.hpp>

using namespace asp::time;
using enum std::memory_order;

static constexpr size_t MAX_BLOCKING_WORKERS = 128;

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

std::shared_ptr<Runtime> Runtime::create(
    size_t workers,
    bool timeDriver,
    bool ioDriver,
    bool signalDriver
) {
    return std::make_shared<Runtime>(ctor_tag{}, workers, timeDriver, ioDriver, signalDriver);
}

Runtime::Runtime(ctor_tag, size_t workers, bool timeDriver, bool ioDriver, bool signalDriver) : m_stopFlag(false) {
    workers = std::clamp<size_t>(workers, 1, 128);

    m_taskDeadline = Duration::fromMillis((uint64_t)(5.f * std::powf(workers, 0.9f)));

    if (timeDriver) {
        m_timeDriver.emplace(weak_from_this());
    }

    if (ioDriver) {
        m_ioDriver.emplace(weak_from_this());
    }

    if (signalDriver) {
        m_signalDriver.emplace(weak_from_this());
    }

    m_workers.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        m_workers.emplace_back(WorkerData{
            .id = i,
        });
    }

    for (size_t i = 0; i < workers; ++i) {
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
    return ctx().runtime();
}

TimeDriver& Runtime::timeDriver() {
    ARC_ASSERT(m_timeDriver, "attempted to use time features with time driver disabled");
    return *m_timeDriver;
}

SignalDriver& Runtime::signalDriver() {
    ARC_ASSERT(m_signalDriver, "attempted to use signal features with signal driver disabled");
    return *m_signalDriver;
}

IoDriver& Runtime::ioDriver() {
    ARC_ASSERT(m_ioDriver, "attempted to use io features with io driver disabled");
    return *m_ioDriver;
}

void Runtime::setTerminateHandler(TerminateHandler handler) {
    auto lock = m_terminateHandler.lock();
    lock = std::move(handler);
}

void Runtime::enqueueTask(TaskBase* task) {
    TRACE("[Runtime] enqueuing task {}", (void*)task);
    {
        std::lock_guard lock(m_mtx);
        m_runQueue.push_back(task);
    }
    m_cv.notify_one();
}

void Runtime::workerLoopWrapper(WorkerData& data) {
    // Wrap around and catch exceptions to get better traces

    try {
        this->workerLoop(data);
    } catch (const std::exception& e) {
        printError("[Worker {}] terminating due to uncaught exception: {}", data.id, e.what());
        ctx().dumpStack();

        auto handler = m_terminateHandler.lock();
        if (*handler) {
            (*handler)(e);
        } else {
            throw; // rethrow
        }
    }
}

void Runtime::workerLoop(WorkerData& data) {
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

    while (true) {
        auto now = Instant::now();

        // every once in a while, run timer and io drivers
        if (m_timeDriver && now >= nextTimerTask) {
            m_timeDriver->doWork();

            do {
                timerTick++;
                nextTimerTask = start + timerOffset + timerTick * timerIncrement;
            } while (now >= nextTimerTask);
        }

        if (m_ioDriver && now >= nextIoTask) {
            m_ioDriver->doWork();

            do {
                ioTick++;
                nextIoTask = start + ioOffset + ioTick * ioIncrement;
            } while (now >= nextIoTask);
        }

        Duration maxWait = Duration::fromHours(1); // arbitrary long duration
        if (m_timeDriver) {
            maxWait = (std::min)(maxWait, nextTimerTask.durationSince(now));
        }
        if (m_ioDriver) {
            maxWait = (std::min)(maxWait, nextIoTask.durationSince(now));
        }

        TaskBase* task = nullptr;
        {
            std::unique_lock lock(m_mtx);
            bool success;

            if (maxWait.isZero()) {
                // don't wait
                success = m_stopFlag.load(::acquire) || !m_runQueue.empty();
            } else {
                success = m_cv.wait_for(lock, std::chrono::microseconds{maxWait.micros()}, [this] {
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


        TRACE("[Worker {}] driving task {}", data.id, (void*)task);
        now = Instant::now();
        auto deadline = now + m_taskDeadline;

        ctx().setTaskDeadline(deadline);
        task->m_vtable->run(task);
        auto taken = now.elapsed();

        TRACE("[Worker {}] finished driving task", data.id);

#ifdef ARC_DEBUG
        if (taken > Duration::fromMillis(100)) {
            printWarn("[Worker {}] task {} took {} to yield", data.id, (void*)task, taken.toString());
        }
#endif
    }
}

void Runtime::blockingWorkerLoop(size_t id) {
    auto lastTask = Instant::now();

    while (true) {
        auto now = Instant::now();

        // terminate if there has been no task in a while
        if (now.durationSince(lastTask) >= Duration::fromSecs(30)) {
            TRACE("[Blocking {}] exiting due to inactivity", id);
            m_blockingWorkers.fetch_sub(1, ::acq_rel);
            m_freeBlockingWorkers.fetch_sub(1, ::acq_rel);
            break;
        }

        BlockingTaskBase* task = nullptr;
        {
            std::unique_lock lock(m_blockingMtx);

            bool success = m_blockingCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
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

        lastTask = now;

        TRACE("[Blocking {}] executing blocking task {}", id, (void*)task);
        m_freeBlockingWorkers.fetch_sub(1, ::acq_rel);
        task->execute();
        m_freeBlockingWorkers.fetch_add(1, ::acq_rel);
        TRACE("[Blocking {}] finished blocking task {}", id, (void*)task);
    }
}

static bool skipRemoveTask = false;
void Runtime::removeTask(TaskBase* task) noexcept {
    if (skipRemoveTask) return;
    m_tasks.lock()->erase(task);
}

void Runtime::ensureBlockingWorker(size_t tasksInQueue) {
    auto workers = m_blockingWorkers.load(::acquire);

    if (workers >= 128) {
        return; // do not spawn more than that
    }

    // spawn a new worker if there are no free workers and there are tasks waiting
    auto freeWorkers = m_freeBlockingWorkers.load(::acquire);
    if (tasksInQueue > 0 && freeWorkers == 0) {
        this->spawnBlockingWorker();
    }
}

void Runtime::spawnBlockingWorker() {
    auto num = m_blockingWorkers.fetch_add(1, ::acq_rel) + 1;
    if (num > MAX_BLOCKING_WORKERS) {
        m_blockingWorkers.fetch_sub(1, ::acq_rel);
        return; // do not spawn more than that
    }

    m_freeBlockingWorkers.fetch_add(1, ::acq_rel);
    size_t workerId = m_nextBlockingWorkerId.fetch_add(1, ::relaxed);

    std::thread th([this, workerId] {
        asp::_setThreadName(fmt::format("arc-blocking-{}", workerId));
        this->blockingWorkerLoop(workerId);
    });
    th.detach();
}

bool Runtime::isShuttingDown() const noexcept {
    return m_stopFlag.load(::acquire);
}

void Runtime::shutdown() {
    TRACE("[Runtime] shutting down");
    m_stopFlag.store(true, ::release);
    m_cv.notify_all();
    m_blockingCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    m_workers.clear();
    m_blockingTasks.clear();

    // deallocate all tasks
    ctx().m_runtime = this;

    skipRemoveTask = true;
    auto tasks = m_tasks.lock();
    for (auto* task : *tasks) {
        task->m_vtable->destroy(task);
    }
    tasks->clear();
    skipRemoveTask = false;

    ctx().m_runtime = nullptr;
}


}