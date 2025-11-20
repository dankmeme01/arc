#include <arc/signal/Signal.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/task/Context.hpp>
#include <signal.h>

namespace arc {

const SignalKind SignalKind::Interrupt{SIGINT};
const SignalKind SignalKind::Terminate{SIGTERM};

// These are not as cross platform lol
#ifdef SIGARM
const SignalKind SignalKind::Alarm{SIGALRM};
#endif
#ifdef SIGCHLD
const SignalKind SignalKind::Child{SIGCHLD};
#endif
#ifdef SIGHUP
const SignalKind SignalKind::Hangup{SIGHUP};
#endif
#ifdef SIGIO
const SignalKind SignalKind::Io{SIGIO};
#endif
#ifdef SIGPIPE
const SignalKind SignalKind::Pipe{SIGPIPE};
#endif
#ifdef SIGQUIT
const SignalKind SignalKind::Quit{SIGQUIT};
#endif
#ifdef SIGUSR1
const SignalKind SignalKind::User1{SIGUSR1};
#endif
#ifdef SIGUSR2
const SignalKind SignalKind::User2{SIGUSR2};
#endif

Signal::Signal(SignalKind kind)
    : m_kind(kind),
      m_notified(ctx().runtime()->signalDriver().addSignalAndNotify(kind)) {}

bool Signal::pollImpl() {
    return m_notified.poll();
}

}