#include <arc/signal/Signal.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/task/Context.hpp>
#include <signal.h>

namespace arc {

const SignalKind SignalKind::Alarm{SIGALRM};
const SignalKind SignalKind::Child{SIGCHLD};
const SignalKind SignalKind::Hangup{SIGHUP};
const SignalKind SignalKind::Interrupt{SIGINT};
const SignalKind SignalKind::Io{SIGIO};
const SignalKind SignalKind::Pipe{SIGPIPE};
const SignalKind SignalKind::Quit{SIGQUIT};
const SignalKind SignalKind::Terminate{SIGTERM};
const SignalKind SignalKind::User1{SIGUSR1};
const SignalKind SignalKind::User2{SIGUSR2};

Signal::Signal(SignalKind kind)
    : m_kind(kind),
      m_notified(ctx().runtime()->signalDriver().addSignalAndNotify(kind)) {}

bool Signal::pollImpl() {
    return m_notified.poll();
}

}