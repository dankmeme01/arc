#pragma once
#include <stdint.h>
#include <atomic>
#include <arc/future/Pollable.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

extern std::atomic<uint64_t> _g_nextTaskLocalKey;

template <typename T>
struct TaskLocalAwaiter : PollableBase {
    using Output = T&;

    inline TaskLocalAwaiter(uint64_t key) : m_key(key) {
        this->m_vtable = &vtable;
    }

    Output await_resume() noexcept {
        MaybeUninit<T*> output;
        reinterpret_cast<void(*)(void*, MaybeUninit<T*>*)>(m_vtable->m_getOutput)(this, &output);
        return *output.assumeInit();
    }

protected:
    uint64_t m_key;
    T* m_entry{};

    static bool vPoll(void* self, Context& cx) noexcept {
        auto me = static_cast<TaskLocalAwaiter*>(self);
        try {
            me->m_entry = &cx.getTLSEntry<T>(me->m_key);
        } catch (...) {
            return true;
        }
        return true;
    }

    static void vGetOutput(void* self, void* outp) {
        auto me = static_cast<TaskLocalAwaiter*>(self);
        ARC_ASSERT(me->m_entry, "task local allocation failed");

        auto out = reinterpret_cast<MaybeUninit<T*>*>(outp);
        out->init(me->m_entry);
    }

    inline static constinit const PollableVtable vtable = [] {
        return PollableVtable {
            .m_poll = &vPoll,
            .m_getOutput = &vGetOutput,
            .m_metadata = PollableMetadata::create<TaskLocalAwaiter>(),
        };
    }();
};

template <typename T>
struct TaskLocalKey {
public:
    TaskLocalKey() noexcept : m_key(_g_nextTaskLocalKey.fetch_add(1, std::memory_order::relaxed)) {}

    T& get(Context& cx) noexcept {
        return cx.getTLSEntry<T>(m_key);
    }

    auto awaiter() noexcept {
        return TaskLocalAwaiter<T>{m_key};
    }

private:
    uint64_t m_key;
};

}
