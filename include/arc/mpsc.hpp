#pragma once

#include "Runtime.hpp"
#include <fmt/core.h>

namespace arc::mpsc {

template <typename T>
struct Receiver;
template <typename T>
struct Sender;

template <typename T>
struct Shared {
    explicit Shared(size_t capacity) : m_capacity(capacity) {}

    bool isClosed() const noexcept {
        return m_closed.load(std::memory_order::acquire);
    }

private:
    std::atomic<size_t> m_senders{0};
    std::atomic<bool> m_closed{false};
    std::coroutine_handle<> m_waiter = nullptr;
    Runtime* m_runtime = nullptr;
    std::deque<T> m_queue;
    std::mutex m_mtx;
    size_t m_capacity;

    friend struct Sender<T>;
    friend struct Receiver<T>;
};

template <typename T>
struct Sender {
    Sender(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {
        m_data->m_senders.fetch_add(1, std::memory_order::relaxed);
    }

    Sender(const Sender& other) : m_data(other.m_data) {
        m_data->m_senders.fetch_add(1, std::memory_order::relaxed);
    }

    Sender& operator=(const Sender& other) {
        if (this != &other) {
            this->releaseData();
            m_data = other.m_data;
            m_data->m_senders.fetch_add(1, std::memory_order::relaxed);
        }
        return *this;
    }

    Sender(Sender&& other) noexcept : m_data(std::exchange(other.m_data, nullptr)) {}

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            this->releaseData();
            m_data = std::exchange(other.m_data, nullptr);
        }
        return *this;
    }

    ~Sender() {
        this->releaseData();
    }

    bool isClosed() const noexcept {
        return m_data->isClosed();
    }

    // Actual send coroutine

    struct SendAwaiter {
        std::shared_ptr<Shared<T>> m_data;
        T m_value;
        bool m_sent = false;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard lock(m_data->m_mtx);

            if (m_data->isClosed()) {
                m_sent = false;
            } else {
                m_sent = true;

                m_data->m_queue.push_back(std::move(m_value));

                if (m_data->m_waiter) {
                    g_runtime->enqueue(m_data->m_waiter);
                    m_data->m_waiter = nullptr;
                }
            }

            g_runtime->enqueue(h);
        }

        bool await_resume() noexcept {
            return m_sent;
        }
    };

    /// Note: until capacity is implemented, this always succeeds and doesn't need to suspend
    auto send(T value) {
        return SendAwaiter{m_data, std::move(value)};
    }

    bool trySend(T value) {
        std::lock_guard lock(m_data->m_mtx);

        if (m_data->isClosed()) {
            return false;
        }

        m_data->m_queue.push_back(std::move(value));

        if (m_data->m_waiter) {
            m_data->m_runtime->enqueue(m_data->m_waiter);
            m_data->m_waiter = nullptr;
        }

        return true;
    }

private:
    std::shared_ptr<Shared<T>> m_data;

    void releaseData() {
        if (!m_data) return;

        if (m_data->m_senders.fetch_sub(1, std::memory_order::acq_rel) == 1) {
            m_data->m_closed.store(true, std::memory_order::release);

            if (m_data->m_waiter) {
                g_runtime->enqueue(m_data->m_waiter);
            }
        }

        m_data.reset();
    }
};

template <typename T>
struct Receiver {
    std::shared_ptr<Shared<T>> m_data;

    Receiver(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {}
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;

    struct RecvAwaiter {
        std::shared_ptr<Shared<T>> m_data;
        std::optional<T> m_out;

        void takeItem() {
            m_out = std::move(m_data->m_queue.front());
            m_data->m_queue.pop_front();
        }

        bool await_ready() {
            std::lock_guard lock(m_data->m_mtx);

            if (!m_data->m_queue.empty()) {
                this->takeItem();
                return true;
            } else if (m_data->isClosed()) {
                // closed + empty -> terminate
                m_out = std::nullopt;
                return true;
            }

            return false;
        }

        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard lock(m_data->m_mtx);

            if (!m_data->m_queue.empty()) {
                this->takeItem();
                g_runtime->enqueue(h); // resume immediately, we have an item
                return;
            } else if (m_data->isClosed()) {
                m_out = std::nullopt;
                g_runtime->enqueue(h); // resume immediately, channel closed
                return;
            }

            m_data->m_waiter = h;
            m_data->m_runtime = g_runtime;
        }

        std::optional<T> await_resume() {
            if (m_out) {
                return std::move(m_out);
            }

            std::lock_guard lock(m_data->m_mtx);
            if (!m_data->m_queue.empty()) {
                this->takeItem();
                return std::move(m_out);
            } else {
                // this should never happen unless the channel was closed
                return std::nullopt;
            }
        }
    };

    auto recv() {
        return RecvAwaiter{m_data};
    }
};


template <typename T>
std::pair<Sender<T>, Receiver<T>> channel(size_t capacity = 0) {
    // TODO: capacity currently unused
    auto shared = std::make_shared<Shared<T>>(capacity);
    return std::make_pair(Sender<T>{shared}, Receiver<T>{shared});
}

}