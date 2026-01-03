#pragma once

#include <arc/signal/Signal.hpp>
#include <arc/sync/Notify.hpp>
#include <asp/sync/SpinLock.hpp>
#include <vector>
#include <memory>

namespace arc {

class Runtime;

class SignalDriver {
public:
    SignalDriver(std::weak_ptr<Runtime> runtime);
    ~SignalDriver();

    Notified addSignalAndNotify(int signum);
    void addSignal(int signum);

private:
    std::weak_ptr<Runtime> m_runtime;
    asp::SpinLock<std::vector<std::pair<int, Notify>>> m_signals;

    size_t addInner(std::vector<std::pair<int, Notify>>& signals, int signum);

    void registerHandler(int signum);
    void handleSignal(int signum);
};

}