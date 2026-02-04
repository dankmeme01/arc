#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_SIGNAL
ARC_FATAL_NO_FEATURE(signal)
#else

#include <arc/signal/Signal.hpp>
#include <arc/sync/Notify.hpp>
#include <asp/sync/SpinLock.hpp>
#include <asp/ptr/SharedPtr.hpp>
#include <vector>

namespace arc {

class Runtime;

class SignalDriver;
struct SignalDriverVtable {
    using AddSignalFn = Notify(*)(SignalDriver*, int);

    AddSignalFn m_addSignal;
};

class SignalDriver {
public:
    SignalDriver(asp::WeakPtr<Runtime> runtime);
    SignalDriver(const SignalDriver&) = delete;
    SignalDriver& operator=(const SignalDriver&) = delete;
    ~SignalDriver();

    Notify addSignal(int signum);

private:
    const SignalDriverVtable* m_vtable;
    asp::WeakPtr<Runtime> m_runtime;
    asp::SpinLock<std::vector<std::pair<int, Notify>>> m_signals;

    static Notify vAddSignal(SignalDriver* self, int signum);

    size_t addInner(std::vector<std::pair<int, Notify>>& signals, int signum);

    void registerHandler(int signum);
    void handleSignal(int signum);
};

}

#endif
