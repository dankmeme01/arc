#pragma once
#include <arc/future/Pollable.hpp>
#include <arc/task/Waker.hpp>
#include <arc/util/Trace.hpp>
#include <asp/sync/SpinLock.hpp>
#include "ChannelBase.hpp"
#include <atomic>
#include <memory>

namespace arc::oneshot {

using namespace arc::chan;

template <typename T>
struct SendAwaiter;
template <typename T>
struct RecvAwaiter;
template <typename T>
using Storage = OneshotStorage<T, RecvAwaiter<T>>;

template <typename T>
struct ChannelData : Storage<T> {
    using Storage<T>::Storage;

    void wakeWaiter() {
        if (this->recvWaiter) {
            this->recvWaiter->m_waker.wake();
            this->recvWaiter = nullptr;
        }
    }
};

template <typename T>
struct Shared {
    explicit Shared() {}

    void receiverDropped() {
        this->close();
    }

    void senderDropped() {
        this->close();
    }

    TrySendOutcome send(T& value) const noexcept(Storage<T>::NoexceptMovable) {
        if (this->isClosed()) {
            return TrySendOutcome::Closed;
        }

        m_data.lock()->push(value);
        return TrySendOutcome::Success;
    }

    Result<T, TryRecvOutcome> tryRecv() const noexcept(Storage<T>::NoexceptMovable) {
        auto value = m_data.lock()->pop();

        if (value) {
            return Ok(std::move(*value));
        }

        return Err(this->isClosed() ? TryRecvOutcome::Closed : TryRecvOutcome::Empty);
    }

    Result<T, TryRecvOutcome> tryRecvOrRegister(RecvAwaiter<T>* awaiter, Context& cx) const noexcept(Storage<T>::NoexceptMovable) {
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

    void deregisterReceiver(RecvAwaiter<T>* awaiter) const {
        m_data.lock()->recvWaiter = nullptr;
    }

    bool isClosed() const noexcept {
        return m_closed.load(std::memory_order::acquire);
    }

private:
    asp::SpinLock<ChannelData<T>> m_data;
    std::atomic<bool> m_closed{false};

    void close() {
        m_closed.store(true, std::memory_order::release);
        m_data.lock()->wakeWaiter();
    }
};


template <typename T>
struct Sender {
    Sender(std::shared_ptr<Shared<T>> data) : m_data(std::move(data)) {}

    ~Sender() {
        if (m_data) m_data->senderDropped();
    }

    Sender(const Sender& other) = delete;
    Sender& operator=(const Sender& other) = delete;
    Sender(Sender&& other) noexcept = default;

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            if (m_data) m_data->senderDropped();
            m_data = std::exchange(other.m_data, nullptr);
        }
        return *this;
    }

    /// Sends a value over the channel. This never blocks and will send the value immediately.
    /// It is undefined behavior to call this more than once. The only possible failure case is if the channel is closed by receiver.
    SendResult<T> send(T value) const noexcept(Storage<T>::NoexceptMovable) {
        auto outcome = m_data->send(value);
        if (outcome == TrySendOutcome::Success) {
            return Ok();
        }

        return Err(std::move(value));
    }

private:
    std::shared_ptr<Shared<T>> m_data;
};

template <typename T>
struct ARC_NODISCARD RecvAwaiter : Pollable<RecvAwaiter<T>, RecvResult<T>, Storage<T>::NoexceptMovable> {
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
    }

    std::optional<RecvResult<T>> poll(Context& cx) noexcept(Storage<T>::NoexceptMovable) {
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
    friend struct OneshotStorage<T, RecvAwaiter<T>>;
    friend struct Shared<T>;
    std::shared_ptr<Shared<T>> m_data;
    Waker m_waker;
    std::optional<T> m_value;
    asp::SpinLock<> m_lock;

    /// Attempts to externally insert the value into this receiver, only works if in the waiting state
    bool tryDeliver(T& value) noexcept(Storage<T>::NoexceptMovable) {
        auto guard = m_lock.lock();
        if (!m_waker || m_value) {
            return false;
        }

        m_value = std::move(value);
        m_waker.wake();
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

    Result<T, TryRecvOutcome> tryRecv() noexcept(Storage<T>::NoexceptMovable) {
        return m_data->tryRecv();
    }

private:
    std::shared_ptr<Shared<T>> m_data;
};

template <typename T>
struct OneshotChannel {
    Sender<T> tx;
    Receiver<T> rx;

    // Wrappers for convenience, it's not uncommon to store the tx+rx parts inside one struct

    /// Sends a value over the channel. This never blocks and will send the value immediately.
    /// It is undefined behavior to call this more than once. The only possible failure case is if the channel is closed by receiver.
    auto send(T value) const {
        return tx.send(std::move(value));
    }

    /// Receives a value from the channel, waiting if there is no value available.
    auto recv() noexcept {
        return rx.recv();
    }

    /// Attempts to receive a value without blocking, returns an error if the channel is empty or closed.
    auto tryRecv() noexcept(ChannelData<T>::NoexceptMovable) {
        return rx.tryRecv();
    }
};

template <typename T>
struct MpscChannel {
    Sender<T> tx;
    Receiver<T> rx;

    // Wrappers for convenience, it's not uncommon to store the tx+rx parts inside one struct

    /// Attempts to send a value, waiting if there is no capacity left.
    /// Returns the value back if the channel is closed.
    auto send(T value) const {
        return tx.send(std::move(value));
    }

    /// Attempts to send a value without blocking, returns the value if the channel is full or closed.
    auto trySend(T value) const {
        return tx.trySend(std::move(value));
    }

    /// Receives a value from the channel, waiting if there is no value available.
    auto recv() const noexcept {
        return rx.recv();
    }

    /// Attempts to receive a value without blocking, returns an error if the channel is empty or closed.
    auto tryRecv() const noexcept(ChannelData<T>::NoexceptMovable) {
        return rx.tryRecv();
    }

    /// Drains all values from the channel into a deque, waking all waiting senders.
    auto drain() {
        return rx.drain();
    }

    bool hasCapacity() const noexcept {
        return tx.hasCapacity();
    }

    bool empty() const noexcept {
        return rx.empty();
    }
};

/// Creates a new oneshot channel. Only one message can ever be sent over the channel, and there can only be one sender and one receiver.
/// The returned value can be destructured into `Sender<T>` and `Receiver<T>` halves to store separately:
/// ```cpp
/// auto [tx, rx] = arc::oneshot::channel<int>();
/// tx.send(42);
/// ```
///
/// `send()` never blocks, and either inserts the message into the channel or straight into the receiver.
/// Calling `send()` more than once is UB, as is calling `recv()`/`tryRecv()` after a message has been received.
///
/// This function does not require a runtime, and can be run in both synchronous and asynchronous contexts.
template <typename T>
OneshotChannel<T> channel() {
    auto shared = std::make_shared<Shared<T>>();
    return OneshotChannel<T>{Sender<T>{shared}, Receiver<T>{shared}};
}

}
