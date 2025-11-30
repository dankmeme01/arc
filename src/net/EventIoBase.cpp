#include <arc/net/EventIoBase.hpp>

namespace arc {

EventIoBase::~EventIoBase() {
    if (!m_io.rio) return;

    ctx().runtime()->ioDriver().unregisterIo(m_io.rio);
}

}