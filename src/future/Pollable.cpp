#include <arc/future/Pollable.hpp>
#include <arc/future/Promise.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

bool PollableBase::await_suspend(std::coroutine_handle<> h) {
    this->attachToParent(h);
    return !m_vtable->poll(this, *this->contextFromParent());
}

void PollableBase::attachToParent(std::coroutine_handle<> h) noexcept {
    m_parent = h;

    auto p = std::coroutine_handle<Promise<void>>::from_address(h.address());
    p.promise().attachChild(this);
}

Context* PollableBase::contextFromParent() const noexcept {
    auto cx = contextFromHandle(m_parent);
    ARC_DEBUG_ASSERT(cx, "context is null in parent");
    return cx;
}

Context* PollableBase::contextFromHandle(std::coroutine_handle<> h) noexcept {
    auto p = std::coroutine_handle<Promise<void>>::from_address(h.address());
    return p.promise().getContext();
}

}