#include <arc/runtime/Runtime.hpp>

namespace arc {

Runtime::Runtime(size_t workers) : m_stopFlag(false) {
    if (workers < 3) {
        workers = 3;
    }

    m_workers.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        m_workers.emplace_back([this, i] {
            if (i == 0) {
                this->timerDriverLoop();
            } else if (i == 1) {
                this->ioDriverLoop();
            } {
                this->workerLoop(i);
            }
        });
    }
}

Runtime::~Runtime() {
    this->shutdown();
}

void Runtime::enqueueTask(TaskBase* task) {
    trace("[Runtime] enqueuing task {}", (void*)task);
    {
        std::lock_guard lock(m_mtx);
        m_runQueue.push_back(task);
    }
    m_cv.notify_one();
}

void Runtime::workerLoop(size_t id) {
    while (true) {
        TaskBase* task = nullptr;
        {
            std::unique_lock lock(m_mtx);
            m_cv.wait(lock, [this] {
                return m_stopFlag.load() || !m_runQueue.empty();
            });

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

        // if (task->m_cancellation.isCancelled()) {
        //     continue;
        // }
        trace("[Worker {}] driving task {}", id, (void*)task);
        task->m_vtable->run(task);
        trace("[Worker {}] finished driving task", id);
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
        if (worker.joinable()) {
            worker.join();
        }
    }

    m_workers.clear();
    m_tasks.clear();
}


}