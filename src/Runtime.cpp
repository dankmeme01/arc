#include <arc/Runtime.hpp>

namespace arc {

Runtime::Runtime(size_t workers) : m_stopFlag(false) {
    if (workers < 2) {
        workers = 2;
    }

    m_workers.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        m_workers.emplace_back([this, i] {
            g_runtime = this;

            if (i == 0) {
                this->timerDriverLoop();
            } else {
                this->workerLoop(i);
            }

            g_runtime = nullptr;
        });
    }
}

Runtime::~Runtime() {
    this->shutdown();
}

void Runtime::enqueue(std::coroutine_handle<> h) {
    {
        std::lock_guard lock(m_mtx);
        m_tasks.push_back(h);
    }
    m_cv.notify_one();
}

void Runtime::workerLoop(size_t id) {
    while (true) {
        std::coroutine_handle<> task;
        {
            std::unique_lock lock(m_mtx);
            m_cv.wait(lock, [this] {
                return m_stopFlag.load() || !m_tasks.empty();
            });

            if (m_stopFlag.load() && m_tasks.empty()) {
                break;
            }

            if (!m_tasks.empty()) {
                task = m_tasks.front();
                m_tasks.pop_front();
            }
        }

        if (task) {
            task.resume();
        }
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

void Runtime::shutdown() {
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