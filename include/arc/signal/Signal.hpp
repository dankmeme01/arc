#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/sync/Notify.hpp>

namespace arc {

struct SignalKind {
    int value;

    constexpr explicit SignalKind(int v) noexcept : value(v) {}

    constexpr inline operator int() const noexcept {
        return value;
    }

    constexpr bool operator==(const SignalKind& other) const noexcept = default;
    constexpr bool operator!=(const SignalKind& other) const noexcept = default;

    static const SignalKind Alarm;
    static const SignalKind Child;
    static const SignalKind Hangup;
    static const SignalKind Interrupt;
    static const SignalKind Io;
    static const SignalKind Pipe;
    static const SignalKind Quit;
    static const SignalKind Terminate;
    static const SignalKind User1;
    static const SignalKind User2;
};

struct Signal : PollableBase<Signal, void> {
    SignalKind m_kind;
    Notified m_notified;

    explicit Signal(SignalKind kind);

    bool pollImpl();
};

inline auto signal(SignalKind kind) {
    return Signal{kind};
}

inline auto ctrl_c() {
    return signal(SignalKind::Interrupt);
}

}