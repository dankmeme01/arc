#include <arc/runtime/Runtime.hpp>
#include <asp/thread/Thread.hpp>

using namespace asp::time;
using enum std::memory_order;

static constexpr size_t MAX_BLOCKING_WORKERS = 128;

namespace arc {

Runtime::Runtime(size_t workers) : m_stopFlag(false) {
    workers = std::clamp<size_t>(workers, 1, 128);

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

void Runtime::enqueueTask(TaskBase* task) {
    trace("[Runtime] enqueuing task {}", (void*)task);
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
        std::terminate();
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
        if (now >= nextTimerTask) {
            m_timeDriver.doWork();

            do {
                timerTick++;
                nextTimerTask = start + timerOffset + timerTick * timerIncrement;
            } while (now >= nextTimerTask);
        }

        if (now >= nextIoTask) {
            m_ioDriver.doWork();

            do {
                ioTick++;
                nextIoTask = start + ioOffset + ioTick * ioIncrement;
            } while (now >= nextIoTask);
        }

        auto maxWait = (std::min)(nextTimerTask, nextIoTask).durationSince(now);

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

        trace("[Worker {}] driving task {}", data.id, (void*)task);
        task->m_vtable->run(task);
        trace("[Worker {}] finished driving task", data.id);
    }
}

void Runtime::blockingWorkerLoop(size_t id) {
    auto lastTask = Instant::now();

    while (true) {
        auto now = Instant::now();

        // terminate if there has been no task in a while
        if (now.durationSince(lastTask) >= Duration::fromSecs(30)) {
            trace("[Blocking {}] exiting due to inactivity", id);
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

        trace("[Blocking {}] executing blocking task {}", id, (void*)task);
        m_freeBlockingWorkers.fetch_sub(1, ::acq_rel);
        task->execute();
        m_freeBlockingWorkers.fetch_add(1, ::acq_rel);
        trace("[Blocking {}] finished blocking task {}", id, (void*)task);
    }
}

void Runtime::removeTask(TaskBase* task) noexcept {
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

void Runtime::shutdown() {
    trace("[Runtime] shutting down");
    m_stopFlag.store(true, ::release);
    m_cv.notify_all();
    m_blockingCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    m_workers.clear();
    m_tasks.lock()->clear();
    m_blockingTasks.clear();
}


}