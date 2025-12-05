#include <arc/runtime/Runtime.hpp>

using namespace asp::time;

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
        worker.thread = std::thread([this, &worker]() {
            this->workerLoop(worker);
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

        auto maxWait = std::min(nextTimerTask, nextIoTask).durationSince(now);

        TaskBase* task = nullptr;
        {
            std::unique_lock lock(m_mtx);
            bool success;

            if (maxWait.isZero()) {
                // don't wait
                success = m_stopFlag.load() || !m_runQueue.empty();
            } else {
                success = m_cv.wait_for(lock, std::chrono::microseconds{maxWait.micros()}, [this] {
                    return m_stopFlag.load() || !m_runQueue.empty();
                });
            }

            if (!success) {
                // timeout
                continue;
            }

            if (m_stopFlag.load() && m_runQueue.empty()) {
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

void Runtime::timerDriverLoop() {
    while (true) {
        m_timeDriver.doWork();

        if (m_stopFlag.load()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Runtime::ioDriverLoop() {
    while (true) {
        m_ioDriver.doWork();

        if (m_stopFlag.load()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Runtime::removeTask(TaskBase* task) noexcept {
    std::lock_guard lock(m_mtx);
    m_tasks.erase(task);
}

void Runtime::shutdown() {
    trace("[Runtime] shutting down");
    m_stopFlag.store(true);
    m_cv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    m_workers.clear();
    m_tasks.clear();
}


}