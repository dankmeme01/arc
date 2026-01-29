#pragma once
#include <arc/util/Result.hpp>
#include <arc/util/Assert.hpp>
#include <deque>
#include <optional>
#include <cstddef>

namespace arc::chan {

struct ClosedError {
    bool operator==(const ClosedError&) const noexcept { return true; }
};

template <typename T>
using RecvResult = Result<T, ClosedError>;
template <typename T>
using SendResult = Result<void, T>;

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

template <typename T, typename SendAwaiter, typename RecvAwaiter>
struct MpscStorage {
    explicit MpscStorage(std::optional<size_t> capacity) : capacity(capacity) {}

    std::deque<T> queue;
    std::deque<SendAwaiter*> sendWaiters;
    RecvAwaiter* recvWaiter = nullptr;
    std::optional<size_t> capacity;

    bool hasCapacity() const noexcept {
        // check for either a receiver, an unbounded channel, or free space in the queue
        return recvWaiter || !capacity || queue.size() < *capacity;
    }

    void clear() {
        queue.clear();
    }

    void registerSendWaiter(SendAwaiter* waiter) {
        sendWaiters.push_back(waiter);
    }

    void registerRecvWaiter(RecvAwaiter* waiter) {
        recvWaiter = waiter;
    }

    /// Pops and returns a value from the queue if one is present.
    /// Unblocks a waiting sender if applicable.
    std::optional<T> pop() {
        if (!queue.empty()) {
            T value = std::move(queue.front());
            queue.pop_front();

            // before returning, wake one waiting sender
            this->unblockSender();

            return value;
        }

        // the queue is empty, let's see if we can snatch a value directly from a sender
        return this->takeFromSender();
    }

    /// Attempts to push a value directly into the receiver's slot if one is present,
    /// otherwise attempts to push into the queue.
    /// If everything fails, returns false.
    bool push(T& value, bool back = true) {
        if (this->deliverToReceiver(value)) {
            return true;
        }

        // push into the queue if possible
        if (!capacity || queue.size() < *capacity) {
            if (back) {
                queue.push_back(std::move(value));
            } else {
                queue.push_front(std::move(value));
            }
            return true;
        }

        return false; // either full or zero capacity; no receiver waiting so can't deliver
    }

private:
    bool deliverToReceiver(T& value) {
        if (recvWaiter && recvWaiter->tryDeliver(value)) {
            recvWaiter = nullptr;
            return true;
        }

        return false;
    }

    /// Unblocks a sender, takes their value and pushes to the queue
    void unblockSender() {
        if (auto val = this->takeFromSender()) {
            queue.push_back(std::move(*val));
        }
    }

    std::optional<T> takeFromSender() {
        if (sendWaiters.empty()) {
            return std::nullopt;
        }

        auto waiter = sendWaiters.front();
        sendWaiters.pop_front();

        // in theory this can never fail?
        return waiter->tryTake();
    }
};

template <typename T, typename RecvAwaiter>
struct OneshotStorage {
    explicit OneshotStorage() {}

    std::optional<T> value;
    RecvAwaiter* recvWaiter = nullptr;

    bool hasCapacity() const noexcept {
        // it is undefined behavior to interact with oneshot after a value has been sent,
        // so this check realistically will always be 'true' for all valid uses.
        return true;
    }

    void registerRecvWaiter(RecvAwaiter* waiter) {
        recvWaiter = waiter;
    }

    /// Returns the value, if present.
    std::optional<T> pop() {
        if (value) {
            return std::move(*value);
        }
        return std::nullopt;
    }

    /// Attempts to push a value directly into the receiver's slot if one is present, otherwise stores it.
    void push(T& value) {
        if (this->deliverToReceiver(value)) {
            return;
        }

        ARC_DEBUG_ASSERT(!this->value.has_value() && "Pushing to a oneshot channel that already has a value!");
        this->value = std::move(value);
    }

private:
    bool deliverToReceiver(T& value) {
        if (recvWaiter && recvWaiter->tryDeliver(value)) {
            recvWaiter = nullptr;
            return true;
        }

        return false;
    }
};

}