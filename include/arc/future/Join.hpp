#pragma once

#include "UtilPollables.hpp"
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <asp/collections/SmallVec.hpp>
#include <ranges>

namespace arc {

template <IsPollable Fut>
struct JoinAllFuture {
    using Output = typename FutureTraits<Fut>::Output;
    static constexpr bool IsVoid = std::is_void_v<Output>;
    using StoredOutput = std::conditional_t<IsVoid, std::monostate, Output>;

    template <typename F>
    JoinAllFuture(F&& fut) : m_future(std::forward<F>(fut)) {}

    JoinAllFuture(JoinAllFuture&&) = default;
    JoinAllFuture& operator=(JoinAllFuture&&) = default;
    JoinAllFuture(const JoinAllFuture&) = delete;
    JoinAllFuture& operator=(const JoinAllFuture&) = delete;

private:
    template <typename FRet, typename VRet, typename... F>
    friend struct JoinAll;
    template <typename FRet, typename F>
    friend struct JoinAllDyn;

    Fut m_future;
    bool m_completed = false;
};

template <typename FOutput, typename NVOutput, typename... Futures>
struct ARC_NODISCARD JoinAll : Pollable<JoinAll<FOutput, NVOutput, Futures...>, std::array<NVOutput, sizeof...(Futures)>> {
    using JoinAllOutput = std::array<NVOutput, sizeof...(Futures)>;
    static constexpr bool IsVoid = std::is_void_v<FOutput>;

    // other 2 arguments simply for inference
    explicit JoinAll(std::tuple<Futures...>&& futs, FOutput*, NVOutput*) : m_futures(std::move(futs)) {}

    std::optional<JoinAllOutput> poll(Context& cx) {
        bool allDone = true;
        this->checkForEach(m_futures, allDone, cx);

        if (!allDone) {
            return std::nullopt;
        }

        return std::make_optional<JoinAllOutput>(this->extractForEach(m_futures));
    }

    template <typename Tuple>
    void checkForEach(Tuple&& t, bool& allDone, Context& cx) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        checkForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, allDone, cx);
    }

    template <size_t... Is, typename Tuple>
    void checkForEachImpl(Tuple&& t, std::index_sequence<Is...>, bool& allDone, Context& cx) {
        (([&]() {
            auto& jafut = std::get<Is>(t);
            if (jafut.m_completed) return;

            auto& fut = jafut.m_future;
            bool ready = fut.m_vtable->poll(&fut, cx);

            if (ready) {
                jafut.m_completed = true;
            } else {
                allDone = false;
            }
        }()), ...);
    }

    template <typename Tuple>
    JoinAllOutput extractForEach(Tuple&& t) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        return extractForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{});
    }

    template <size_t... Is, typename Tuple>
    JoinAllOutput extractForEachImpl(Tuple&& t, std::index_sequence<Is...>) {
        return JoinAllOutput{([&]() {
            auto& jafut = std::get<Is>(t);
            auto& fut = jafut.m_future;

            if constexpr (IsVoid) {
                // propagate exceptions, return monostate
                fut.m_vtable->template getOutput<void>(&fut);
                return NVOutput{};
            } else {
                return fut.m_vtable->template getOutput<FOutput>(&fut);
            }
        }())...};
    }

private:
    std::tuple<Futures...> m_futures;
};

template <typename Out>
struct JoinAllDynOutputType_ {
    static constexpr size_t SmallVecSize = 256;
    static constexpr size_t SmallCap = SmallVecSize / sizeof(Out);

    using type = asp::SmallVec<Out, SmallCap>;
};
template <>
struct JoinAllDynOutputType_<void> {
    using type = void;
};
template <typename FRet>
using JoinAllDynOutputType = typename JoinAllDynOutputType_<FRet>::type;

template <typename FRet, typename Fut>
struct ARC_NODISCARD JoinAllDyn : Pollable<JoinAllDyn<FRet, Fut>, JoinAllDynOutputType<FRet>> {
    using TransformedFut = JoinAllFuture<Fut>;
    using JoinAllOutput = JoinAllDynOutputType<FRet>;
    using PollOutput = std::conditional_t<std::is_void_v<JoinAllOutput>, bool, std::optional<JoinAllOutput>>;

    static constexpr bool IsVoid = std::is_void_v<FRet>;
    static constexpr size_t SmallVecSize = 256;
    static constexpr size_t SmallCap = SmallVecSize / sizeof(JoinAllFuture<Fut>);

    template <typename Cont>
    explicit JoinAllDyn(Cont&& futs) {
        m_futures.reserve(std::ranges::distance(futs));
        for (auto& fut : futs) {
            m_futures.emplace_back(TransformedFut{std::move(fut)});
        }
        futs.clear();
    }

    PollOutput poll(Context& cx) {
        bool allDone = true;

        for (size_t i = 0; i < m_futures.size(); i++) {
            auto& jafut = m_futures[i];
            auto& fut = jafut.m_future;

            if (jafut.m_completed) continue;

            bool ready = fut.m_vtable->poll(&fut, cx);
            if (ready) {
                jafut.m_completed = true;
            } else {
                allDone = false;
            }
        }

        if (!allDone) {
            if constexpr (IsVoid) {
                return false;
            } else {
                return std::nullopt;
            }
        }

        return this->extractOutputs<>();
    }

private:
    asp::SmallVec<TransformedFut, SmallCap> m_futures;

    template <bool Void_ = IsVoid>
    auto extractOutputs();

    template <>
    auto extractOutputs<true>() {
        // propagate exceptions
        for (auto& jafut : m_futures) {
            auto& fut = jafut.m_future;
            fut.m_vtable->template getOutput<void>(&fut);
        }
        return true;
    }

    template <>
    auto extractOutputs<false>() {
        JoinAllOutput out;
        out.reserve(m_futures.size());

        for (auto& jafut : m_futures) {
            auto& fut = jafut.m_future;
            out.emplace_back(
                fut.m_vtable->template getOutput<FRet>(&fut)
            );
        }

        return std::make_optional<JoinAllOutput>(std::move(out));
    }
};

template <typename F, typename... Rest>
struct MultiFutureExtractRet {
    using Output = typename FutureTraits<std::decay_t<F>>::Output;
    using NVOutput = std::conditional_t<std::is_void_v<Output>, std::monostate, Output>;
};

template <typename Expected, typename F, typename... Rest>
constexpr void _validateJoinAllOutputType() {
    using FOut = typename FutureTraits<std::decay_t<F>>::Output;
    static_assert(std::is_same_v<FOut, Expected>, "All futures passed to joinAll must have the same output type");

    if constexpr (sizeof...(Rest) > 0) {
        _validateJoinAllOutputType<Expected, Rest...>();
    }
}

template <typename... Fx>
auto joinAll(Fx&&... futs) {
    // Extract output type, expect it to be the same for all futures
    using Output = typename MultiFutureExtractRet<Fx...>::Output;
    using NVOutput = typename MultiFutureExtractRet<Fx...>::NVOutput;
    _validateJoinAllOutputType<Output, Fx...>();

    auto fs = std::make_tuple(JoinAllFuture<std::decay_t<Fx>>{std::forward<Fx>(futs)}...);

    return JoinAll{std::move(fs), static_cast<Output*>(nullptr), static_cast<NVOutput*>(nullptr)};
}

template <>
inline auto joinAll() {
    return arc::ready(std::array<std::monostate, 0>{});
}

template <typename Cont> requires std::ranges::input_range<Cont> && IsPollable<std::ranges::range_value_t<Cont>>
auto joinAll(Cont&& futs) {
    using Fut = std::ranges::range_value_t<Cont>;
    using Output = typename FutureTraits<Fut>::Output;
    using JAFut = JoinAllFuture<Fut>;
    using NVOutput = typename JAFut::StoredOutput;

    return JoinAllDyn<Output, Fut>{std::forward<Cont>(futs)};
}

}
