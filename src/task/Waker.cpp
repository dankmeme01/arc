#include <arc/task/Waker.hpp>
#include <arc/util/Assert.hpp>

namespace arc {

bool RawWaker::equals(const RawWaker& other) const noexcept {
    return m_data == other.m_data && m_vtable == other.m_vtable;
}

bool RawWaker::valid() const noexcept {
    return m_vtable != nullptr;
}

RawWaker RawWaker::noop() noexcept {
    static constexpr RawWakerVtable vtable = {
        .wake = [](void*) {},
        .wakeByRef = [](void*) {},
        .clone = [](void*) -> RawWaker {
            return RawWaker::noop();
        },
        .destroy = [](void*) {},
    };

    return RawWaker{nullptr, &vtable};
}

Waker::Waker(void* data, const RawWakerVtable* vtable) noexcept : RawWaker{data, vtable} {}

Waker::Waker(RawWaker raw) noexcept : RawWaker(raw) {}

Waker::Waker(Waker&& other) noexcept {
    m_data = other.m_data;
    m_vtable = other.m_vtable;
    other.reset();
}

Waker& Waker::operator=(Waker&& other) noexcept {
    if (this != &other) {
        this->destroy();
        m_data = other.m_data;
        m_vtable = other.m_vtable;
        other.reset();
    }

    return *this;
}

Waker::operator bool() const noexcept {
    return this->valid();
}

Waker::~Waker() {
    this->destroy();
}

void Waker::reset() noexcept {
    m_data = nullptr;
    m_vtable = nullptr;
}

void Waker::destroy() {
    if (!this->valid()) return;
    m_vtable->destroy(m_data);
    this->reset();
}

Waker Waker::noop() noexcept {
    return Waker{RawWaker::noop()};
}

void Waker::wake() noexcept {
    ARC_ASSERT(this->valid(), "invalid waker used");
    m_vtable->wake(m_data);
    this->reset();
}

void Waker::wakeByRef() noexcept {
    ARC_ASSERT(this->valid(), "invalid waker used");
    m_vtable->wakeByRef(m_data);
}

Waker Waker::clone() const noexcept {
    ARC_ASSERT(this->valid(), "invalid waker used");
    return m_vtable->clone(m_data);
}

}
