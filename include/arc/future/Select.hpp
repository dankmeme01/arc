#pragma once

#include "Future.hpp"
#include <arc/task/Task.hpp>
#include <arc/util/Trace.hpp>
#include <std23/function_ref.h>

namespace arc {

template <typename Fut, typename Out = typename FutureTraits<std::decay_t<Fut>>::Output>
Future<Out> _toPlainFuture(Fut fut) {
    co_return co_await std::move(fut);
}

template <typename Output, typename Cbt>
struct Selectee {
    using Callback = std::conditional_t<std::is_void_v<Output>,
        std23::function_ref<Cbt()>,
        // this is funny, but it's necessary to avoid a void param error
        std23::function_ref<Cbt(std::conditional_t<std::is_void_v<Output>, std::monostate, Output>)>
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
struct Select : PollableBase<Select<Futures...>> {
    std::optional<std::tuple<Futures...>> m_selectees;
    size_t m_winner = static_cast<size_t>(-1);

    explicit Select(std::tuple<Futures...>&& selectees) : m_selectees(std::move(selectees)) {}

    bool poll() {
        this->checkForEach(*m_selectees);
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

    if constexpr (std::is_void_v<Output>) {
        using Cbt = decltype(callback());
        return Selectee<Output, Cbt>{std::move(fut), std::move(callback), enabled};
    } else {
        using Cbt = decltype(callback(std::declval<Output>()));
        return Selectee<Output, Cbt>{std::move(fut), std::move(callback), enabled};
    }
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
arc::Future<> invokeSelecteeCallback(std::index_sequence<Is...>, auto& sel) {
    (co_await ([&] -> arc::Future<> {
        auto& selectee = std::get<Is>(*sel.m_selectees);
        if (sel.m_winner != Is) co_return;

        if constexpr (selectee.IsVoid) {
            using CbRet = decltype(selectee.callback());
            if constexpr (IsFuture<CbRet>::value) {
                co_await selectee.callback();
            } else {
                selectee.callback();
            }
        } else {
            auto output = selectee.future.getOutput();
            using CbRet = decltype(selectee.callback(output));
            if constexpr (IsFuture<CbRet>::value) {
                co_await selectee.callback(std::move(output));
            } else {
                selectee.callback(std::move(output));
            }
        }
    }()), ...);
    co_return;
}

template <typename... Futures>
arc::Future<> select(Futures... futs) {
    auto selectees = std::make_tuple(Selectee{std::move(futs)}...);
    Select sel{std::move(selectees)};

    co_await sel;
    co_await invokeSelecteeCallback(std::make_index_sequence<sizeof...(Futures)>{}, sel);
}

}