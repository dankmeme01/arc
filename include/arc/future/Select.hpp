#pragma once

#include "Future.hpp"
#include <arc/task/Task.hpp>
#include <arc/Util.hpp>
#include <std23/function_ref.h>

namespace arc {

template <typename Fut, typename Out = typename FutureTraits<std::decay_t<Fut>>::Output>
Future<Out> _toPlainFuture(Fut fut) {
    co_return co_await std::move(fut);
}

template <typename Output>
struct Selectee {
    using Callback = std::conditional_t<std::is_void_v<Output>,
        std23::function_ref<void()>,
        // this is funny, but it's necessary to avoid a void param error
        std23::function_ref<void(std::conditional_t<std::is_void_v<Output>, std::monostate, Output>)>
    >;
    static constexpr bool IsVoid = std::is_void_v<Output>;

    Future<Output> future;
    Callback callback;
    bool active = true;

    template <typename F>
    Selectee(F fut, Callback cb, bool act) : future(_toPlainFuture(std::move(fut))), callback(cb), active(act) {}

    Selectee(Selectee&&) = default;
    Selectee& operator=(Selectee&&) = default;
    Selectee(const Selectee&) = delete;
    Selectee& operator=(const Selectee&) = delete;
};

template <typename... Futures>
struct Select : PollableBase<Select<Futures...>, void> {
    std::optional<std::tuple<Futures...>> m_selectees;
    size_t m_winner = static_cast<size_t>(-1);
    std::coroutine_handle<> m_awaiter;

    explicit Select(std::tuple<Futures...>&& selectees) : m_selectees(std::move(selectees)) {}

    bool poll() {
        checkForEach(*m_selectees);
        return m_winner != static_cast<size_t>(-1);
    }

    template <typename Tuple>
    void checkForEach(Tuple&& t) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        checkForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{});
    }

    template <size_t... Is, typename Tuple>
    void checkForEachImpl(Tuple&& t, std::index_sequence<Is...>) {
        (([&]() {
            if (m_winner != static_cast<size_t>(-1)) return;
            auto& selectee = std::get<Is>(t);

            trace("[Select] checking selectee {}, active: {}", Is, selectee.active);
            if (selectee.active) {
                auto res = selectee.future.poll();
                if (res) {
                    m_winner = Is;
                    trace("[Select] selectee {} finished!", Is);
                }
            }
        }()), ...);
    }

};

template <typename Fut>
auto selectee(Fut fut, auto callback, bool enabled) {
    using Output = typename FutureTraits<std::decay_t<Fut>>::Output;
    return Selectee<Output>{std::move(fut), std::move(callback), enabled};
}

template <typename Fut>
auto selectee(Fut fut, auto callback) {
    return selectee(std::move(fut), std::move(callback), true);
}

template <typename Fut>
auto selectee(Fut fut) {
    return selectee(std::move(fut), [](auto...) {}, true);
}

// Alias to selectee
template <typename... Args>
auto branch(Args&&... args) {
    return selectee(std::forward<Args>(args)...);
}

template <size_t... Is>
auto invokeSelecteeCallback(std::index_sequence<Is...>, auto& sel) {
    (([&] {
        auto& selectee = std::get<Is>(*(sel.m_selectees));

        if (sel.m_winner == Is) {
            if constexpr (selectee.IsVoid) {
                selectee.callback();
            } else {
                selectee.callback(selectee.future.getOutput());
            }
        }
    }()), ...);
}

template <typename... Futures>
arc::Future<> selectInner(bool biased, Futures... futs) {
    auto selectees = std::make_tuple(Selectee{std::move(futs)}...);
    Select sel{std::move(selectees)};

    co_await sel;

    invokeSelecteeCallback(std::make_index_sequence<sizeof...(Futures)>{}, sel);
}

struct biased_t {};
inline constexpr biased_t biased{};

template <typename... Futures>
auto select(biased_t, Futures... futs) {
    return selectInner(true, std::move(futs)...);
}

template <typename... Futures>
auto select(Futures... futs) {
    return selectInner(false, std::move(futs)...);
}

}
