#include <arc/runtime/SignalDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Assert.hpp>
#include <csignal>

namespace arc {

SignalDriver::SignalDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    static const SignalDriverVtable vtable {
        .m_addSignal = &SignalDriver::vAddSignal,
    };
    m_vtable = &vtable;
}

SignalDriver::~SignalDriver() {}

Notify SignalDriver::addSignal(int signum) {
    return m_vtable->m_addSignal(this, signum);
}

Notify SignalDriver::vAddSignal(SignalDriver* self, int signum) {
    auto lock = self->m_signals.lock();
    size_t index = self->addInner(*lock, signum);
    return (*lock)[index].second;
}

size_t SignalDriver::addInner(std::vector<std::pair<int, Notify>>& signals, int signum) {
    // check if we already have this signal
    for (size_t i = 0; i < signals.size(); ++i) {
        if (signals[i].first == signum) {
            return i;
        }
    }

    signals.emplace_back(signum, Notify{});
    this->registerHandler(signum);
    return signals.size() - 1;
}

void SignalDriver::registerHandler(int signum) {
#ifndef _WIN32
    ARC_ASSERT(signum != SIGKILL && signum != SIGSTOP, "Cannot register handler for SIGKILL or SIGSTOP");
#endif

    static SignalDriver* self;
    self = this;

    std::signal(signum, [](int s) {
        self->handleSignal(s);
    });
}

void SignalDriver::handleSignal(int signum) {
    auto lock = m_signals.lock();

    for (auto& [sig, notify] : *lock) {
        if (sig == signum) {
            notify.notifyAll();
            break;
        }
    }
}

}
