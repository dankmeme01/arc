#include <arc/future/Pollable.hpp>
#include <arc/future/Promise.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

bool PollableBase::await_suspend(std::coroutine_handle<> h) {
    this->attachToParent(h);
    return !m_vtable->poll(this, *this->contextFromParent());
}

void PollableBase::attachToParent(std::coroutine_handle<> h) {
    m_parent = h;

    auto p = std::coroutine_handle<Promise<void>>::from_address(h.address());
    p.promise().attachChild(this);
}

Context* PollableBase::contextFromParent() const noexcept {
    auto p = std::coroutine_handle<Promise<void>>::from_address(m_parent.address());
    Context* cx = p.promise().getContext();
    ARC_DEBUG_ASSERT(cx, "context is null in parent");
    return cx;
}

}