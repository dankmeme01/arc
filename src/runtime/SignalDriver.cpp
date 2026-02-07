#include <arc/runtime/SignalDriver.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Assert.hpp>
#include <asp/collections/SmallVec.hpp>
#include <csignal>

namespace arc {

/// The signal manager is static across multiple runtimes
class SignalManager {
public:
    static SignalManager& get() {
        static SignalManager manager;
        return manager;
    }

    void registerSignal(int signum, SignalDriver* driver) {
#ifndef _WIN32
    ARC_ASSERT(signum != SIGKILL && signum != SIGSTOP, "Cannot register handler for SIGKILL or SIGSTOP");
#endif

        auto signals = m_signals.lock();

        auto it = signals->find(signum);
        if (it != signals->end()) {
            it->second.emplace_back(driver);
        } else {
            signals->emplace(signum, std::vector{driver});
            this->setupHandler(signum);
        }
    }

private:
    asp::SpinLock<std::unordered_map<int, std::vector<SignalDriver*>>> m_signals;

    void setupHandler(int sig) {
        std::signal(sig, [](int s) {
            auto& manager = SignalManager::get();
            manager.invoke(s);
        });
    }

    void invoke(int sig) {
        auto lock = m_signals.lock();

        auto it = lock->find(sig);
        if (it != lock->end()) {
            for (auto driver : it->second) {
                driver->handleSignal(sig);
            }
        }
    }
};

SignalDriver::SignalDriver(asp::WeakPtr<Runtime> runtime) : m_runtime(std::move(runtime)) {
    static constexpr SignalDriverVtable vtable {
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
    SignalManager::get().registerSignal(signum, this);
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
