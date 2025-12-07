#include <arc/future/Pollable.hpp>
#include <arc/future/Future.hpp>

namespace arc {

bool PollableUniBase::await_suspend(std::coroutine_handle<> h) {
    auto awaitingP = std::coroutine_handle<Promise<void>>::from_address(h.address());
    awaitingP.promise().m_child = this;
    auto res = this->vPoll();
    return !res;
}

}