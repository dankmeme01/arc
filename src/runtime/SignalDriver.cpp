#include <arc/runtime/SignalDriver.hpp>
#include <arc/Util.hpp>
#include <csignal>

namespace arc {

SignalDriver::SignalDriver(Runtime* runtime) : m_runtime(runtime) {}

SignalDriver::~SignalDriver() {}

Notified SignalDriver::addSignalAndNotify(int signum) {
    auto lock = m_signals.lock();
    size_t index = this->addInner(*lock, signum);
    return (*lock)[index].second.notified();
}

void SignalDriver::addSignal(int signum) {
    auto lock = m_signals.lock();
    this->addInner(*lock, signum);
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
