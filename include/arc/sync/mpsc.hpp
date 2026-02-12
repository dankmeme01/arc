#pragma once
#include <arc/future/Pollable.hpp>
#include <arc/task/Waker.hpp>
#include <arc/util/Trace.hpp>
#include <asp/sync/SpinLock.hpp>
#include "ChannelBase.hpp"
#include <atomic>
#include <memory>

namespace arc::mpsc {

using namespace arc::chan;

template <typename T>
struct SendAwaiter;
template <typename T>
struct RecvAwaiter;
template <typename T>
using Storage = MpscStorage<T, SendAwaiter<T>, RecvAwaiter<T>>;

template <typename T>
struct ChannelData : Storage<T> {
    using Storage<T>::Storage;

    void wakeAll() {
        if (this->recvWaiter) {
            this->recvWaiter->m_waker->wake();
        }

        for (auto& waiter : this->sendWaiters) {
            waiter->m_waker->wake();
        }

        this->recvWaiter = nullptr;
        this->sendWaiters.clear();
    }
};

template <typename T>
struct Shared {
    explicit Shared(std::optional<size_t> capacity) : m_data(ChannelData<T>(capacity)) {}

    bool isClosed() const noexcept {
        return m_closed.load(std::memory_order::acquire);
    }

    bool hasCapacity() const noexcept {
        return m_data.lock()->hasCapacity();
    }

    void receiverDropped() {
        this->close();
    }

    void senderDropped() {
        if (m_senders.fetch_sub(1, std::memory_order::acq_rel) == 1) {
            this->close();
        }
    }

    void senderCloned()  {
        m_senders.fetch_add(1, std::memory_order::relaxed);
    }

    TrySendOutcome trySend(T& value) const {
        if (this->isClosed()) {
            return TrySendOutcome::Closed;
        }

        bool success = m_data.lock()->push(value);
        return success ? TrySendOutcome::Success : TrySendOutcome::Full;
    }

    TrySendOutcome trySendAtFront(T& value) const {
        if (this->isClosed()) {
            return TrySendOutcome::Closed;
        }

        bool success = m_data.lock()->push(value, false);
        return success ? TrySendOutcome::Success : TrySendOutcome::Full;
    }

    TrySendOutcome trySendOrRegister(SendAwaiter<T>* awaiter, Context& cx) {
        if (this->isClosed()) {
            return TrySendOutcome::Closed;
        }

        ARC_DEBUG_ASSERT(awaiter->m_value && !awaiter->m_waker);
        T& value = *awaiter->m_value;

        auto data = m_data.lock();
        if (data->push(value)) {
            awaiter->m_value.reset();
            return TrySendOutcome::Success;
        }

        // register the awaiter
        if (!awaiter->m_waker) {
            awaiter->m_waker = cx.cloneWaker();
        }

        data->registerSendWaiter(awaiter);
        return TrySendOutcome::Full;
    }

    void deregisterSender(SendAwaiter<T>* awaiter) noexcept {
        auto data = m_data.lock();
        auto it = std::find(data->sendWaiters.begin(), data->sendWaiters.end(), awaiter);
        if (it != data->sendWaiters.end()) {
            data->sendWaiters.erase(it);
        }
    }

    Result<T, TryRecvOutcome> tryRecv() noexcept(ChannelData<T>::NoexceptMovable) {
        auto value = m_data.lock()->pop();

        if (value) {
            return Ok(std::move(*value));
        }

        return Err(this->isClosed() ? TryRecvOutcome::Closed : TryRecvOutcome::Empty);
    }

    Result<T, TryRecvOutcome> tryRecvOrRegister(RecvAwaiter<T>* awaiter, Context& cx) noexcept(ChannelData<T>::NoexceptMovable) {
        auto data = m_data.lock();
        auto value = data->pop();

        if (value) {
            return Ok(std::move(*value));
        }

        if (this->isClosed()) {
            return Err(TryRecvOutcome::Closed);
        }

        // register the awaiter
        if (!awaiter->m_waker) {
            awaiter->m_waker = cx.cloneWaker();
        }

        data->registerRecvWaiter(awaiter);
        return Err(TryRecvOutcome::Empty);
    }

    void deregisterReceiver(RecvAwaiter<T>* awaiter) noexcept {
        m_data.lock()->recvWaiter = nullptr;
    }

    std::deque<T> drain() {
        std::deque<T> empty;
        auto data = m_data.lock();
        empty.swap(data->queue);

        data->wakeAll();
        return empty;
    }

    bool empty() const noexcept {
        return m_data.lock()->queue.empty();
    }

private:
    std::atomic<size_t> m_senders{0};
    std::atomic<bool> m_closed{false};
    asp::SpinLock<ChannelData<T>> m_data;

    void close() {
        m_closed.store(true, std::memory_order::release);
        m_data.lock()->wakeAll();
    }
};

template <typename T>
struct ARC_NODISCARD SendAwaiter : Pollable<SendAwaiter<T>, SendResult<T>> {
    explicit SendAwaiter(std::shared_ptr<Shared<T>> data, T value)
        : m_data(std::move(data)), m_value(std::move(value)) {}

    SendAwaiter(SendAwaiter&& other) noexcept
        : m_data(std::move(other.m_data)),
          m_value(std::move(other.m_value)),
          m_waker(std::move(other.m_waker))
    {
        ARC_ASSERT(!m_waker && m_value, "cannot move a SendAwaiter that already was polled");
    }

    SendAwaiter& operator=(SendAwaiter&& other) noexcept = delete;

    ~SendAwaiter() {
        // if we are in the waiting state, remove ourselves from the wait list
        // note: locking m_lock here isnt necessary and is also unsafe and can lead to deadlocks
        if (m_data) m_data->deregisterSender(this);
    }

    std::optional<SendResult<T>> poll(Context& cx) {
        // We have two valid states for being polled, and the 3rd completed state:
        // 1. Initial state, m_value is set, m_waker is not set
        // 2. Waiting state, m_value is set, m_waker is set
        // 3. Done state, m_value is not set, m_waker is anything
        auto guard = m_lock.lock();

        if (m_value && !m_waker) {
            // handle initial state
            auto outcome = m_data->trySendOrRegister(this, cx);
            switch (outcome) {
                case TrySendOutcome::Success: {
                    return Ok();
                } break;

                case TrySendOutcome::Closed: {
                    return Err(std::move(*m_value));
                } break;

                case TrySendOutcome::Full: {
                    return std::nullopt; // waiting ..
                } break;
            }

            std::unreachable();
        } else if (m_value && m_waker) {
            // handle waiting state
            // since receivers are cooperative and will notify us asap when we can push the value,
            // the only thing to do is to check if the channel is closed
            if (m_data->isClosed()) {
                return Err(std::move(*m_value));
            }

            return std::nullopt;
        } else {
            // completed!
            return Ok();
        }
    }

private:
    friend struct ChannelData<T>;
    friend struct MpscStorage<T, SendAwaiter<T>, RecvAwaiter<T>>;
    friend struct Shared<T>;
    std::shared_ptr<Shared<T>> m_data;
    std::optional<Waker> m_waker;
    std::optional<T> m_value;
    asp::SpinLock<> m_lock;

    /// Attempts to externally take the value from this sender, only works if in the waiting state
    std::optional<T> tryTake() noexcept(ChannelData<T>::NoexceptMovable) {
        auto guard = m_lock.lock();
        if (!m_waker || !m_value) {
            return std::nullopt;
        }

        T value = std::move(*m_value);
        m_value.reset();
        m_waker->wake();
        return value;
    }
};

template <typename T>
struct Sender {
    Sender(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {
        m_data->senderCloned();
    }

    ~Sender() {
        if (m_data) m_data->senderDropped();
    }

    Sender(const Sender& other) : m_data(other.m_data) {
        m_data->senderCloned();
    }

    Sender& operator=(const Sender& other) {
        if (this != &other) {
            if (m_data) m_data->senderDropped();
            m_data = other.m_data;
            m_data->senderCloned();
        }
        return *this;
    }

    Sender(Sender&& other) noexcept : m_data(std::exchange(other.m_data, nullptr)) {}

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            if (m_data) m_data->senderDropped();
            m_data = std::exchange(other.m_data, nullptr);
        }
        return *this;
    }

    /// Attempts to send a value, waiting if there is no capacity left.
    /// Returns the value back if the channel is closed.
    SendAwaiter<T> send(T value) const {
        return SendAwaiter<T>{m_data, std::move(value)};
    }

    /// Attempts to send a value without blocking, returns the value if the channel is full or closed.
    SendResult<T> trySend(T value) const {
        auto outcome = m_data->trySend(value);
        if (outcome == TrySendOutcome::Success) {
            return Ok();
        }

        return Err(std::move(value));
    }

    /// Checks if the channel has any capacity to accept new messages.
    /// Note that this is only a hint, if this returns `true` there is no
    /// guarantee that a subsequent `send()` will succeed without blocking.
    bool hasCapacity() const noexcept {
        return m_data->hasCapacity();
    }

private:
    std::shared_ptr<Shared<T>> m_data;
};

template <typename T>
struct ARC_NODISCARD RecvAwaiter : Pollable<RecvAwaiter<T>, RecvResult<T>, ChannelData<T>::NoexceptMovable> {
    explicit RecvAwaiter(std::shared_ptr<Shared<T>> data) noexcept
        : m_data(std::move(data)) {}

    RecvAwaiter(RecvAwaiter&& other) noexcept
        : m_data(std::move(other.m_data)),
            m_value(std::move(other.m_value)),
            m_waker(std::move(other.m_waker))
    {
        ARC_ASSERT(!m_waker && !m_value, "cannot move a RecvAwaiter that already was polled");
    }

    RecvAwaiter& operator=(RecvAwaiter&& other) noexcept = delete;

    ~RecvAwaiter() {
        // if we are in the waiting state, remove ourselves from the wait list
        if (m_data) m_data->deregisterReceiver(this);

        if (m_value && m_data) {
            // we got destroyed while holding a value, this is rare but not good,
            // try to put the value back into the channel to avoid data loss
            auto outcome = m_data->trySendAtFront(*m_value);
            if (outcome != TrySendOutcome::Success) {
                printWarn("RecvAwaiter destroyed while holding a value, could not reinsert into channel!");
            }
        }
    }

    std::optional<RecvResult<T>> poll(Context& cx) noexcept(ChannelData<T>::NoexceptMovable) {
        // We have two valid states for being polled, and the 3rd completed state:
        // 1. Initial state, m_value is not set, m_waker is not set
        // 2. Waiting state, m_value is not set, m_waker is set
        // 3. Done state, m_value is set, m_waker is anything
        // Polling again after the done state is undefined behavior.
        auto guard = m_lock.lock();

        if (!m_value && !m_waker) {
            // handle initial state
            auto res = m_data->tryRecvOrRegister(this, cx);
            if (res) {
                // immediately received, complete the future
                return Ok(std::move(res).unwrap());
            }

            auto outcome = res.unwrapErr();
            switch (outcome) {
                case TryRecvOutcome::Closed: {
                    return Err(ClosedError{});
                } break;

                case TryRecvOutcome::Empty: {
                    return std::nullopt; // waiting ..
                } break;

                default: std::unreachable();
            }
        } else if (!m_value && m_waker) {
            // handle waiting state, once again, nothing to do here besides check for closure
            if (m_data->isClosed()) {
                return Err(ClosedError{});
            }

            return std::nullopt;
        } else {
            // completed!
            auto val = std::move(*m_value);
            m_value.reset();
            return Ok(std::move(val));
        }
    }

private:
    friend struct ChannelData<T>;
    friend struct MpscStorage<T, SendAwaiter<T>, RecvAwaiter<T>>;
    friend struct Shared<T>;
    std::shared_ptr<Shared<T>> m_data;
    std::optional<Waker> m_waker;
    std::optional<T> m_value;
    asp::SpinLock<> m_lock;

    /// Attempts to externally insert the value into this receiver, only works if in the waiting state
    bool tryDeliver(T& value) {
        auto guard = m_lock.lock();
        if (!m_waker || m_value) {
            return false;
        }

        m_value = std::move(value);
        m_waker->wake();
        return true;
    }
};

template <typename T>
struct Receiver {
    Receiver(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {}
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept = default;

    Receiver& operator=(Receiver&& other) noexcept {
        if (this != &other) {
            if (m_data) m_data->receiverDropped();
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    ~Receiver() {
        if (m_data) m_data->receiverDropped();
    }

    RecvAwaiter<T> recv() noexcept {
        return RecvAwaiter<T>{m_data};
    }

    Result<T, TryRecvOutcome> tryRecv() noexcept(ChannelData<T>::NoexceptMovable) {
        return m_data->tryRecv();
    }

    std::deque<T> drain() {
        return m_data->drain();
    }

    bool empty() const noexcept {
        return m_data->empty();
    }

private:
    std::shared_ptr<Shared<T>> m_data;
};

/// Creates a new multi-producer, single-consumer channel with the given capacity.
/// Sender<T> may be copied to create multiple senders, but there can only be one Receiver<T>.
/// `send()` is typically non-blocking unless the channel is full, in which case senders will have to wait.
/// When capacity is `std::nullopt` (the default), the channel is unbounded.
/// When capacity is set to 0, the channel is rendezvous,
/// meaning that messages are never stored and can only be sent when a receiver is waiting.
///
/// This function does not require a runtime, and can be run in both synchronous and asynchronous contexts.
template <typename T>
std::pair<Sender<T>, Receiver<T>> channel(std::optional<size_t> capacity = std::nullopt) {
    auto shared = std::make_shared<Shared<T>>(capacity);
    return std::make_pair(Sender<T>{shared}, Receiver<T>{shared});
}

}
