#pragma once

#include "Future.hpp"
#include <arc/task/Task.hpp>
#include <arc/util/Trace.hpp>
#include <arc/util/Function.hpp>

namespace arc {

template <typename Output, typename Cbt>
struct Selectee {
    using Callback = std::conditional_t<std::is_void_v<Output>,
        arc::MoveOnlyFunction<Cbt()>,
        // this is funny, but it's necessary to avoid a void param error
        arc::MoveOnlyFunction<Cbt(std::conditional_t<std::is_void_v<Output>, std::monostate, Output>)>
    >;
    static constexpr bool IsVoid = std::is_void_v<Output>;

    template <typename F>
    Selectee(F fut, Callback cb, bool act) : future(toPlainFuture(std::move(fut))), callback(std::move(cb)), active(act) {
#ifdef ARC_DEBUG
        auto [ptr, len] = getTypename<F>();
        std::string_view futname{ptr, len};
        future.setDebugName(fmt::format("Select future ({})", futname));
#endif
    }

    Selectee(Selectee&&) = default;
    Selectee& operator=(Selectee&&) = default;
    Selectee(const Selectee&) = delete;
    Selectee& operator=(const Selectee&) = delete;

private:
    template <typename... F>
    friend struct Select;

    Future<Output> future;
    Callback callback;
    bool active = true;
};

template <typename... Futures>
struct ARC_NODISCARD Select : Pollable<Select<Futures...>> {
    explicit Select(std::tuple<Futures...>&& selectees) : m_selectees(std::move(selectees)) {}

    bool poll(Context& cx) {
        this->checkForEach(*m_selectees, cx);
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
            if (m_winner != static_cast<size_t>(-1)) return;
            auto& selectee = std::get<Is>(t);

            // ARC_TRACE("[Select] checking selectee {}, active: {}", Is, selectee.active);
            if (selectee.active) {
                auto res = selectee.future.poll(cx);
                if (res) {
                    m_winner = Is;
                    // ARC_TRACE("[Select] selectee {} finished!", Is);
                }
            }
        }()), ...);
    }

    template <size_t... Is>
    arc::Future<> invokeSelecteeCallback(std::index_sequence<Is...>) {
        (co_await ([&] -> arc::Future<> {
            auto& selectee = std::get<Is>(*m_selectees);
            using Sel = std::decay_t<decltype(selectee)>;

            if (m_winner != Is) co_return;

            if constexpr (Sel::IsVoid) {
                selectee.future.getOutput(); // rethrow exceptions

                using CbRet = decltype(selectee.callback());
                if constexpr (IsPollable<CbRet>) {
                    co_await selectee.callback();
                } else {
                    selectee.callback();
                }
            } else {
                auto output = selectee.future.getOutput();

                using CbRet = decltype(selectee.callback(output));
                if constexpr (IsPollable<CbRet>) {
                    co_await selectee.callback(std::move(output));
                } else {
                    selectee.callback(std::move(output));
                }
            }
        }()), ...);
        co_return;
    }

private:
    std::optional<std::tuple<Futures...>> m_selectees;
    size_t m_winner = static_cast<size_t>(-1);
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

template <typename... Futures>
arc::Future<> select(Futures... futs) {
    auto selectees = std::make_tuple(Selectee{std::move(futs)}...);
    Select sel{std::move(selectees)};

    co_await sel;
    co_await sel.invokeSelecteeCallback(std::make_index_sequence<sizeof...(Futures)>{});
}

}