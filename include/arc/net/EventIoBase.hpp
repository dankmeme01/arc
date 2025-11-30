#include <arc/runtime/IoDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/future/Pollable.hpp>

namespace arc {

class EventIoBase {
public:
    EventIoBase(Registration io) : m_io(std::move(io)) {}

    ~EventIoBase();

protected:
    Registration m_io;
};


}