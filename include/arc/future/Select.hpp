#pragma once

#include "Pollable.hpp"
#include <arc/util/Trace.hpp>
#include <arc/util/Function.hpp>

namespace arc {

template <IsPollable Fut, typename Cbt>
struct Selectee {
    using Output = FutureTraits<Fut>::Output;
    using Callback = std::conditional_t<std::is_void_v<Output>,
        arc::MoveOnlyFunction<Cbt()>,
        // this is funny, but it's necessary to avoid a void param error
        arc::MoveOnlyFunction<Cbt(std::conditional_t<std::is_void_v<Output>, std::monostate, Output>)>
    >;
    static constexpr bool IsVoid = std::is_void_v<Output>;
    static constexpr bool AsyncCallback = IsPollable<Cbt>;

    Selectee(Fut&& fut, Callback&& cb, bool act) : m_future(std::move(fut)), m_callback(std::move(cb)), m_active(act) {}

    Selectee(Selectee&&) = default;
    Selectee& operator=(Selectee&&) = default;
    Selectee(const Selectee&) = delete;
    Selectee& operator=(const Selectee&) = delete;

    bool pollCallback(Context& cx) {
        auto invokeCallback = [&] {
            if constexpr (IsVoid) {
                // propagate exceptions
                m_future.m_vtable->template getOutput<void>(&m_future);
                return m_callback();
            } else {
                return m_callback(m_future.m_vtable->template getOutput<Output>(&m_future));
            }
        };

        if constexpr (!AsyncCallback) {
            // sync callback, immediately ready
            invokeCallback();
            return true;
        } else {
            // async callback, invoke lambda once to obtain the Future
            if (!m_callbackFuture) {
                m_callbackFuture.emplace(invokeCallback());
            }

            auto& cbf = *m_callbackFuture;
            if (!cbf.m_vtable->poll(&cbf, cx)) {
                return false; // pending
            }

            using CallbackRet = FutureTraits<Cbt>::Output;

            // callback is ready! propagate exceptions and return
            cbf.m_vtable->template getOutput<CallbackRet>(&cbf);
            return true;
        }
    }

private:
    template <typename... F>
    friend struct Select;

    Fut m_future;
    Callback m_callback;
    ARC_NO_UNIQUE_ADDRESS std::conditional_t<AsyncCallback, std::optional<Cbt>, std::monostate> m_callbackFuture;
    bool m_active = true;
};

template <typename... Branches>
struct ARC_NODISCARD Select : Pollable<Select<Branches...>> {
    explicit Select(std::tuple<Branches...>&& selectees) : m_selectees(std::move(selectees)) {}

    bool poll(Context& cx) {
        // if no branch has won yet, poll them all until a completion
        if (!this->hasWinner()) {
            this->checkForEach(m_selectees, cx);
        }

        // if still no winning branch, return pending
        if (!this->hasWinner()) {
            return false;
        }

        // already have a winner, we now need to invoke / poll callback now
        return this->pollCallbackForEach(m_selectees, cx);
    }

private:
    std::tuple<Branches...> m_selectees;
    size_t m_winner = static_cast<size_t>(-1);
    
    bool hasWinner() const noexcept {
        return m_winner != static_cast<size_t>(-1);
    }

    template <typename Tuple>
    void checkForEach(Tuple&& t, Context& cx) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        checkForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, cx);
    }

    template <size_t... Is, typename Tuple>
    void checkForEachImpl(Tuple&& t, std::index_sequence<Is...>, Context& cx) {
        (([&]() {
            if (this->hasWinner()) return;
            auto& selectee = std::get<Is>(t);

            if (!selectee.m_active) return;

            auto& fut = selectee.m_future;
            bool ready = fut.m_vtable->poll(&fut, cx);
            if (ready) {
                m_winner = Is;
            }
        }()), ...);
    }
    
    template <typename Tuple>
    bool pollCallbackForEach(Tuple&& t, Context& cx) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        return pollCallbackForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, cx);
    }
    
    template <size_t... Is, typename Tuple>
    bool pollCallbackForEachImpl(Tuple&& t, std::index_sequence<Is...>, Context& cx) {
        bool result;

        (([&]() {
            auto& selectee = std::get<Is>(t);
            using Sel = std::decay_t<decltype(selectee)>;
            if (m_winner != Is) return;
            
            result = selectee.pollCallback(cx);
        }()), ...);

        return result;
    }
};

template <typename RawFut>
auto selectee(RawFut&& fut, auto callback, bool enabled) {
    using Fut = std::decay_t<RawFut>;
    using Output = typename FutureTraits<Fut>::Output;

    if constexpr (std::is_void_v<Output>) {
        using Cbt = decltype(callback());
        return Selectee<Fut, Cbt>{std::forward<RawFut>(fut), std::move(callback), enabled};
    } else {
        using Cbt = decltype(callback(std::declval<Output>()));
        return Selectee<Fut, Cbt>{std::forward<RawFut>(fut), std::move(callback), enabled};
    }
}

template <typename Fut>
auto selectee(Fut&& fut, auto callback) {
    return selectee(std::forward<Fut>(fut), std::move(callback), true);
}

template <typename Fut>
auto selectee(Fut&& fut) {
    return selectee(std::forward<Fut>(fut), [](auto...) {}, true);
}

// Alias to selectee
template <typename... Args>
auto branch(Args&&... args) {
    return selectee(std::forward<Args>(args)...);
}

template <typename... Selectees>
auto select(Selectees&&... futs) {
    auto selectees = std::make_tuple(Selectee{std::forward<Selectees>(futs)}...);
    return Select{std::move(selectees)};
}

}