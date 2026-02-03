#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_SIGNAL
ARC_FATAL_NO_FEATURE(signal)
#else

#include <arc/future/Pollable.hpp>
#include <arc/sync/Notify.hpp>
#include <signal.h>

namespace arc {

struct SignalKind {
    int value;

    constexpr explicit SignalKind(int v) noexcept : value(v) {}

    constexpr inline operator int() const noexcept {
        return value;
    }

    constexpr bool operator==(const SignalKind& other) const noexcept = default;
    constexpr bool operator!=(const SignalKind& other) const noexcept = default;

    static const SignalKind Interrupt;
    static const SignalKind Terminate;

#ifdef SIGALRM
    static const SignalKind Alarm;
#endif
#ifdef SIGCHLD
    static const SignalKind Child;
#endif
#ifdef SIGHUP
    static const SignalKind Hangup;
#endif
#ifdef SIGIO
    static const SignalKind Io;
#endif
#ifdef SIGPIPE
    static const SignalKind Pipe;
#endif
#ifdef SIGQUIT
    static const SignalKind Quit;
#endif
#ifdef SIGUSR1
    static const SignalKind User1;
#endif
#ifdef SIGUSR2
    static const SignalKind User2;
#endif
};

struct ARC_NODISCARD Signal : Pollable<Signal> {
    explicit Signal(SignalKind kind);
    bool poll(Context& cx);

private:
    SignalKind m_kind;
    std::optional<Notified> m_notified;
};

inline auto signal(SignalKind kind) {
    return Signal{kind};
}

inline auto ctrl_c() {
    return signal(SignalKind::Interrupt);
}

}

#endif
