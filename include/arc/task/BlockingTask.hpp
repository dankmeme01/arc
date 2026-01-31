#pragma once
#include <arc/future/Future.hpp>
#include <arc/task/Waker.hpp>
#include <arc/util/Function.hpp>

#include <asp/sync/SpinLock.hpp>
#include <asp/ptr/SharedPtr.hpp>

namespace arc {

struct BlockingTaskVtable {
    using ExecuteFn = void(*)(void*);

    ExecuteFn execute = nullptr;
};

struct BlockingTaskBase {
    asp::WeakPtr<Runtime> m_runtime;
    const BlockingTaskVtable* m_vtable;

    BlockingTaskBase(asp::WeakPtr<Runtime> runtime, const BlockingTaskVtable* vtable)
        : m_runtime(std::move(runtime)), m_vtable(vtable) {}

    void execute() {
        m_vtable->execute(this);
    }
};

template <typename T>
struct BlockingTask : BlockingTaskBase {
    static std::shared_ptr<BlockingTask> create(asp::WeakPtr<Runtime> runtime, arc::MoveOnlyFunction<T()> func) {
        return std::make_shared<BlockingTask>(std::move(runtime), std::move(func));
    }

    BlockingTask(asp::WeakPtr<Runtime> runtime, arc::MoveOnlyFunction<T()> func)
        : BlockingTaskBase(std::move(runtime), &vtable), m_func(std::move(func)) {}

    std::optional<T> pollTask(Context& cx) {
        auto data = m_data.lock();

        if (data->m_result) {
            auto out = std::move(*data->m_result);
            data->m_result.reset();
            return out;
        } else {
            auto myWaker = cx.waker();
            if (!data->m_awaiter || !data->m_awaiter->equals(*myWaker)) {
                data->m_awaiter = myWaker->clone();
            }

            return std::nullopt;
        }
    }

private:
    struct Data {
        std::optional<T> m_result;
        std::optional<Waker> m_awaiter;
    };

    arc::MoveOnlyFunction<T()> m_func;
    asp::SpinLock<Data> m_data;

    static void vExecute(void* ptr) {
        auto* task = static_cast<BlockingTask*>(ptr);
        T result = task->m_func();

        auto data = task->m_data.lock();
        data->m_result = std::move(result);

        if (data->m_awaiter) {
            data->m_awaiter->wake();
            data->m_awaiter.reset();
        }
    }

    static constexpr BlockingTaskVtable vtable = {
        .execute = &BlockingTask::vExecute,
    };
};

// Specialization for void
template <>
struct BlockingTask<void> : BlockingTaskBase {
    static std::shared_ptr<BlockingTask> create(asp::WeakPtr<Runtime> runtime, arc::MoveOnlyFunction<void()> func) {
        return std::make_shared<BlockingTask>(std::move(runtime), std::move(func));
    }

    BlockingTask(asp::WeakPtr<Runtime> runtime, arc::MoveOnlyFunction<void()> func)
        : BlockingTaskBase(std::move(runtime), &vtable), m_func(std::move(func)) {}

    bool pollTask(Context& cx) {
        auto data = m_data.lock();

        if (data->m_completed) {
            return true;
        } else {
            auto myWaker = cx.waker();
            if (!data->m_awaiter || !data->m_awaiter->equals(*myWaker)) {
                data->m_awaiter = myWaker->clone();
            }

            return false;
        }
    }

private:
    struct Data {
        bool m_completed = false;
        std::optional<Waker> m_awaiter;
    };

    arc::MoveOnlyFunction<void()> m_func;
    asp::SpinLock<Data> m_data;

    static void vExecute(void* ptr) {
        auto* task = static_cast<BlockingTask*>(ptr);
        task->m_func();

        auto data = task->m_data.lock();
        data->m_completed = true;

        if (data->m_awaiter) {
            data->m_awaiter->wake();
            data->m_awaiter.reset();
        }
    }

    static constexpr BlockingTaskVtable vtable = {
        .execute = &BlockingTask::vExecute,
    };
};

template <typename T>
struct BlockingTaskHandle : Pollable<BlockingTaskHandle<T>, T> {
public:
    BlockingTaskHandle(std::shared_ptr<BlockingTask<T>> task) : m_task(std::move(task)) {}
    BlockingTaskHandle(const BlockingTaskHandle&) = delete;
    BlockingTaskHandle& operator=(const BlockingTaskHandle&) = delete;
    BlockingTaskHandle(BlockingTaskHandle&& other) noexcept = default;
    BlockingTaskHandle& operator=(BlockingTaskHandle&& other) noexcept = default;

    auto poll(Context& cx) {
        return m_task->pollTask(cx);
    }
private:
    friend class Runtime;
    std::shared_ptr<BlockingTask<T>> m_task;
};

}
