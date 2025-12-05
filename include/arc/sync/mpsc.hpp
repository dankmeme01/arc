#pragma once
#include <arc/future/Pollable.hpp>
#include <arc/task/Context.hpp>
#include <arc/task/Waker.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Result.hpp>
#include <asp/sync/SpinLock.hpp>
#include <Geode/Result.hpp>
#include <atomic>
#include <memory>
#include <deque>

namespace arc::mpsc {

struct ChannelClosed {};

template <typename T>
using ChannelRecvResult = Result<T, ChannelClosed>;
template <typename T>
using ChannelSendResult = Result<void, T>;

enum class TrySendOutcome {
    Success,
    Full,
    Closed
};

enum class TryRecvOutcome {
    Success,
    Empty,
    Closed
};

template <typename T>
struct Shared {
    explicit Shared(std::optional<size_t> capacity) : m_capacity(capacity) {}

    bool isClosed() const noexcept {
        return m_data.lock()->closed;
    }

    void incSenders() {
        m_senders.fetch_add(1, std::memory_order::relaxed);
    }

    void decSenders() {
        if (m_senders.fetch_sub(1, std::memory_order::acq_rel) == 1) {
            m_data.lock()->handleClosure();
        }
    }

    template <bool RemoveWaker = false>
    Result<T, TryRecvOutcome> tryRecv() {
        auto data = m_data.lock();
        auto value = data->pop();

        if (value) {
            data->unblockSender();
            if constexpr (RemoveWaker) {
                data->recvWaiter.reset();
            }
            return Ok(std::move(*value));
        }

        if (data->closed) {
            ARC_DEBUG_ASSERT(!data->recvWaiter, "channel closure should've already removed recvWaiter");
            return Err(TryRecvOutcome::Closed);
        }

        return Err(TryRecvOutcome::Empty);
    }

    void drain() {
        auto data = m_data.lock();
        while (!data->queue.empty()) {
            data->pop();
            data->unblockSender();
        }
    }

    Result<T, TryRecvOutcome> recvOrWait(std::optional<T>* valueSlot) {
        auto data = m_data.lock();
        auto value = data->pop();

        if (value) {
            data->unblockSender();
            return Ok(std::move(*value));
        }

        if (data->closed) {
            return Err(TryRecvOutcome::Closed);
        }

        data->recvWaiter = {ctx().cloneWaker(), valueSlot};

        return Err(TryRecvOutcome::Empty);
    }

    template <bool AddWaker = false, bool RemoveWaker = false>
    TrySendOutcome trySend(T& value, uint64_t* waiterId = nullptr) {
        auto data = m_data.lock();

        if (data->closed) {
            return TrySendOutcome::Closed;
        }

        auto removeWaker = [&] {
            if (!RemoveWaker || !waiterId) {
                return;
            }

            // find and remove the given waker from the sendWaiters
            auto it = std::find_if(
                data->sendWaiters.begin(),
                data->sendWaiters.end(),
                [waiterId](const auto& waiter) {
                    return waiter.second == *waiterId;
                }
            );

            if (it != data->sendWaiters.end()) {
                data->sendWaiters.erase(it);
            }
        };

        // check if a receiver is waiting, put value directly into its slot and wake them
        if (data->recvWaiter) {
            std::optional<T>* slot = data->recvWaiter->second;
            if (slot && !slot->has_value()) {
                *slot = std::move(value);
                data->recvWaiter->first.wake();
                data->recvWaiter.reset();

                removeWaker();
                return TrySendOutcome::Success;
            }
        }

        if (!m_capacity.has_value() || (*m_capacity != 0 && data->queue.size() >= *m_capacity)) {
            if constexpr (AddWaker) {
                ARC_DEBUG_ASSERT(waiterId);

                if (*waiterId != 0) {
                    // check if already registered
                    auto it = std::find_if(
                        data->sendWaiters.begin(),
                        data->sendWaiters.end(),
                        [waiterId](const auto& waiter) {
                            return waiter.second == *waiterId;
                        }
                    );

                    if (it != data->sendWaiters.end()) {
                        // already registered, do nothing
                        return TrySendOutcome::Full;
                    }
                }

                auto id = nextId();
                data->sendWaiters.push_back({ctx().cloneWaker(), id});
                if (waiterId) {
                    *waiterId = id;
                }
            }

            return TrySendOutcome::Full;
        }

        data->queue.push_back(std::move(value));

        // wake any waiting receiver
        if (data->recvWaiter) {
            data->recvWaiter->first.wake();
        }

        removeWaker();
        return TrySendOutcome::Success;
    }

    void unregisterRecvWaiter() {
        auto data = m_data.lock();
        data->recvWaiter.reset();
    }

    void unregisterSendWaiter(uint64_t id) {
        auto data = m_data.lock();
        data->sendWaiters.erase(
            std::remove_if(
                data->sendWaiters.begin(),
                data->sendWaiters.end(),
                [id](const auto& waiter) {
                    return waiter.second == id;
                }
            ),
            data->sendWaiters.end()
        );
    }

    void receiverClosed() {
        m_data.lock()->handleClosure();
    }

private:
    struct Data {
        std::deque<T> queue;
        std::optional<std::pair<Waker, std::optional<T>*>> recvWaiter;
        std::deque<std::pair<Waker, uint64_t>> sendWaiters;
        bool closed = false;

        ~Data() {
            // sanity check
            ARC_DEBUG_ASSERT(!recvWaiter && sendWaiters.empty(), "Data destroyed while having waiters");
        }

        std::optional<T> pop() {
            if (queue.empty()) {
                return std::nullopt;
            }
            T value = std::move(queue.front());
            queue.pop_front();
            return value;
        }

        void unblockSender() {
            if (!sendWaiters.empty()) {
                auto [waker, _] = std::move(sendWaiters.front());
                sendWaiters.pop_front();
                waker.wake();
            }
        }

        void handleClosure() {
            closed = true;

            if (recvWaiter) {
                recvWaiter->first.wake();
            }

            for (auto& [waker, _] : sendWaiters) {
                waker.wake();
            }

            recvWaiter.reset();
            sendWaiters.clear();
        }
    };

    std::optional<size_t> m_capacity;
    std::atomic<size_t> m_senders{0};
    asp::SpinLock<Data> m_data;

    uint64_t nextId() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order::relaxed);
    }
};

template <typename T>
struct Sender {
    Sender(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {
        m_data->incSenders();
    }

    Sender(const Sender& other) : m_data(other.m_data) {
        m_data->incSenders();
    }

    Sender& operator=(const Sender& other) {
        if (this != &other) {
            this->releaseData();
            m_data = other.m_data;
            m_data->incSenders();
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

    struct Awaiter : PollableBase<Awaiter, ChannelSendResult<T>> {
        explicit Awaiter(std::shared_ptr<Shared<T>> data, T value)
            : m_data(std::move(data)), m_value(std::move(value)) {}

        Awaiter(Awaiter&& other) noexcept
            : m_data(std::exchange(other.m_data, nullptr)),
              m_state(other.m_state),
              m_waiterId(other.m_waiterId),
              m_value(std::move(other.m_value))
        {
            other.m_state = State::Done;
            other.m_waiterId = 0;
        }

        Awaiter& operator=(Awaiter&& other) noexcept = delete;

        std::optional<ChannelSendResult<T>> poll() {
            switch (m_state) {
                case State::Init: {
                    auto outcome = m_data->template trySend<true>(m_value, &m_waiterId);
                    if (outcome == TrySendOutcome::Success) {
                        m_state = State::Done;
                        return Ok();
                    } else if (outcome == TrySendOutcome::Closed) {
                        m_state = State::Done;
                        return Err(std::move(m_value));
                    }

                    // channel is full, we need to wait
                    m_state = State::Waiting;
                    return std::nullopt;
                } break;

                case State::Waiting: {
                    auto outcome = m_data->template trySend<true, true>(m_value, &m_waiterId);
                    if (outcome == TrySendOutcome::Success) {
                        m_state = State::Done;
                        return Ok();
                    } else if (outcome == TrySendOutcome::Closed) {
                        m_state = State::Done;
                        return Err(std::move(m_value));
                    }

                    return std::nullopt;
                } break;

                case State::Done: {
                    ARC_UNREACHABLE("polled after completion");
                } break;
            }
        }

        ~Awaiter() {
            this->unregister();
        }
    private:
        enum class State {
            Init,
            Waiting,
            Done
        };

        State m_state{State::Init};
        std::shared_ptr<Shared<T>> m_data;
        uint64_t m_waiterId = 0;
        T m_value;

        void unregister() {
            if (m_state == State::Waiting) {
                m_data->unregisterSendWaiter(m_waiterId);
            }
        }
    };

    /// Attempts to send a value, waiting if there is no capacity left.
    /// Returns the value back if the channel is closed.
    Awaiter send(T value) {
        return Awaiter{m_data, std::move(value)};
    }

    /// Attempts to send a value without blocking, returns the value if the channel is full or closed.
    ChannelSendResult<T> trySend(T value) {
        auto outcome = m_data->trySend(value);
        if (outcome == TrySendOutcome::Success) {
            return Ok();
        } else {
            return Err(std::move(value));
        }
    }

private:
    std::shared_ptr<Shared<T>> m_data;

    void releaseData() {
        if (!m_data) return;
        m_data->decSenders();
        m_data.reset();
    }
};

template <typename T>
struct Receiver {
    Receiver(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {}
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;

    ~Receiver() {
        if (m_data) m_data->receiverClosed();
    }

    struct Awaiter : PollableBase<Awaiter, ChannelRecvResult<T>> {
        explicit Awaiter(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {}

        Awaiter(Awaiter&& other) noexcept : m_data(std::exchange(other.m_data, nullptr)), m_state(other.m_state) {
            other.m_state = State::Done;
        }

        Awaiter& operator=(Awaiter&& other) noexcept = delete;

        std::optional<ChannelRecvResult<T>> poll() {
            switch (m_state) {
                case State::Init: {
                    // if the channel has a value, return it immediately
                    auto value = m_data->recvOrWait(&m_valueSlot);
                    if (value) {
                        m_state = State::Done;
                        return Ok(std::move(value).unwrap());
                    }

                    // check if the channel is closed
                    auto err = value.unwrapErr();
                    if (err == TryRecvOutcome::Closed) {
                        m_state = State::Done;
                        return Err(ChannelClosed{});
                    }

                    // otherwise, we are waiting
                    m_state = State::Waiting;
                    return std::nullopt;
                } break;

                case State::Waiting: {
                    if (m_valueSlot) {
                        m_state = State::Done;
                        return Ok(std::move(*m_valueSlot));
                    }

                    auto value = m_data->template tryRecv<true>();
                    if (value) {
                        m_state = State::Done;
                        return Ok(std::move(*value));
                    }

                    // check if the channel is closed
                    auto err = value.unwrapErr();
                    if (err == TryRecvOutcome::Closed) {
                        m_state = State::Done;
                        return Err(ChannelClosed{});
                    }

                    return std::nullopt;
                } break;

                case State::Done: {
                    ARC_UNREACHABLE("polled after completion");
                } break;
            }
        }

        ~Awaiter() {
            if (m_state == State::Waiting) {
                m_data->unregisterRecvWaiter();
            }
        }

    private:
        enum class State {
            Init,
            Waiting,
            Done
        };

        std::shared_ptr<Shared<T>> m_data;
        std::optional<T> m_valueSlot;
        State m_state{State::Init};
    };

    Awaiter recv() {
        return Awaiter{m_data};
    }

    Result<T, TryRecvOutcome> tryRecv() {
        return m_data->tryRecv();
    }

    void drain() {
        m_data->drain();
    }

private:
    std::shared_ptr<Shared<T>> m_data;
};

/// Creates a new multi-producer, single-consumer channel with the given capacity.
/// Sender<T> may be copied to create multiple senders, but there can only be one Receiver<T>.
/// `send()` is typically non-blocking unless the channel is full, in which case senders will have to wait.
/// When capacity is 0 (the default), the channel is unbounded.
/// When capacity is set to `std::nullopt`, the channel is zero-capacity (rendezvous),
/// meaning that messages are never stored and can only be sent when a receiver is waiting.
template <typename T>
std::pair<Sender<T>, Receiver<T>> channel(std::optional<size_t> capacity = 0) {
    auto shared = std::make_shared<Shared<T>>(capacity);
    return std::make_pair(Sender<T>{shared}, Receiver<T>{shared});
}

}

