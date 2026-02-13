#pragma once

#include "Future.hpp"
#include <arc/util/Assert.hpp>

#if 0
# define TRACE ::arc::trace
#else
# define TRACE(...) do {} while(0)
#endif

namespace arc {

template <typename Output>
struct JoinAllFuture {
    static constexpr bool IsVoid = std::is_void_v<Output>;
    using StoredOutput = std::conditional_t<IsVoid, std::monostate, Output>;

    template <typename F>
    JoinAllFuture(F fut) : future(toPlainFuture(std::move(fut))) {}

    JoinAllFuture(JoinAllFuture&&) = default;
    JoinAllFuture& operator=(JoinAllFuture&&) = default;
    JoinAllFuture(const JoinAllFuture&) = delete;
    JoinAllFuture& operator=(const JoinAllFuture&) = delete;

private:
    template <typename FRet, typename... F>
    friend struct JoinAll;
    template <typename FRet, typename Fut>
    friend struct JoinAllDyn;

    Future<Output> future;
    std::optional<StoredOutput> output;
};

template <typename FRet, typename... Futures>
struct ARC_NODISCARD JoinAll : Pollable<JoinAll<FRet, Futures...>, std::array<FRet, sizeof...(Futures)>> {
    using JoinAllOutput = std::array<FRet, sizeof...(Futures)>;
    using JoinAllTempOutput = std::array<std::optional<FRet>, sizeof...(Futures)>;
    explicit JoinAll(std::tuple<Futures...>&& futs, FRet*) : m_futures(std::move(futs)) {}

    // helper to convert from array<optional<T>, N> to array<T, N>
    static auto convertTempOutput(JoinAllTempOutput&& tempOut) {
        return std::apply([](auto&&... opts) {
            return JoinAllOutput{std::move(*opts)...};
        }, std::move(tempOut));
    }

    std::optional<JoinAllOutput> poll(Context& cx) {
        bool allDone = true;
        this->checkForEach(*m_futures, allDone, cx);

        if (!allDone) {
            return std::nullopt;
        }

        JoinAllTempOutput out;
        this->extractForEach(*m_futures, out);

        return std::make_optional<JoinAllOutput>(convertTempOutput(std::move(out)));
    }

    template <typename Tuple>
    void checkForEach(Tuple&& t, bool& allDone, Context& cx) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        checkForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, allDone, cx);
    }

    template <size_t... Is, typename Tuple>
    void checkForEachImpl(Tuple&& t, std::index_sequence<Is...>, bool& allDone, Context& cx) {
        (([&]() {
            auto& fut = std::get<Is>(t);

            TRACE("[JoinAll] checking future {}, active: {}", Is, !fut.output.has_value());
            if (!fut.output) {
                auto res = fut.future.poll(cx);
                if (res) {
                    fut.output = fut.future.getOutput();
                    TRACE("[JoinAll] future {} finished!", Is);
                } else {
                    allDone = false;
                }
            }
        }()), ...);
    }

    template <typename Tuple>
    void extractForEach(Tuple&& t, JoinAllTempOutput& out) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        extractForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, out);
    }

    template <size_t... Is, typename Tuple>
    void extractForEachImpl(Tuple&& t, std::index_sequence<Is...>, JoinAllTempOutput& out) {
        (([&]() {
            auto& fut = std::get<Is>(t);
            using Fut = std::decay_t<decltype(fut)>;

            if constexpr (!Fut::IsVoid) {
                out[Is] = std::move(*fut.output);
            }
        }()), ...);
    }

private:
    std::optional<std::tuple<Futures...>> m_futures;
};

template <typename Out>
struct JoinAllDynOutputType_ {
    using type = std::vector<Out>;
};
template <>
struct JoinAllDynOutputType_<void> {
    using type = void;
};
template <typename FRet>
using JoinAllDynOutputType = typename JoinAllDynOutputType_<FRet>::type;

template <typename FRet, typename Fut>
struct ARC_NODISCARD JoinAllDyn : Pollable<JoinAllDyn<FRet, Fut>, JoinAllDynOutputType<FRet>> {
    using JoinAllOutput = JoinAllDynOutputType<FRet>;
    using PollOutput = std::conditional_t<std::is_void_v<JoinAllOutput>, bool, std::optional<JoinAllOutput>>;
    using TransformedFut = JoinAllFuture<FRet>;
    static constexpr bool IsVoid = std::is_void_v<JoinAllOutput>;

    explicit JoinAllDyn(std::vector<Fut>&& futs, FRet*) {
        for (auto& fut : futs) {
            m_futures.emplace_back(TransformedFut{std::move(fut)});
        }
        futs.clear();
    }

    PollOutput poll(Context& cx) {
        bool allDone = true;

        for (size_t i = 0; i < m_futures.size(); i++) {
            auto& fut = m_futures[i];

            TRACE("[JoinAll] checking future {}, active: {}", i, !fut.output.has_value());

            if (!fut.output) {
                auto res = fut.future.poll(cx);
                if (res) {
                    if constexpr (!IsVoid) {
                        fut.output = fut.future.getOutput();
                    }
                    TRACE("[JoinAll] future {} finished!", i);
                } else {
                    allDone = false;
                }
            }
        }

        if constexpr (IsVoid) {
            return allDone;
        } else {
            if (!allDone) {
                return std::nullopt;
            }

            JoinAllOutput out;
            out.reserve(m_futures.size());

            for (auto& fut : m_futures) {
                if constexpr (!TransformedFut::IsVoid) {
                    out.emplace_back(std::move(*fut.output));
                }
            }

            return std::make_optional<JoinAllOutput>(std::move(out));
        }
    }

private:
    std::vector<TransformedFut> m_futures;
};

template <typename F, typename... Rest>
struct MultiFutureExtractRet {
    using type = typename FutureTraits<std::decay_t<F>>::Output;
};

template <typename... Futures>
auto joinAll(Futures... futs) {
    // Extract output type, expect it to be the same for all futures
    using Output = typename MultiFutureExtractRet<Futures...>::type;
    using JoinAllFuture = JoinAllFuture<Output>;
    using NVOutput = typename JoinAllFuture::StoredOutput;

    auto fs = std::make_tuple(JoinAllFuture{std::move(futs)}...);
    return JoinAll{std::move(fs), static_cast<NVOutput*>(nullptr)};
}

template <typename Fut>
auto joinAll(std::vector<Fut> futs) {
    using Output = typename FutureTraits<std::decay_t<Fut>>::Output;
    using JoinAllFuture = JoinAllFuture<Output>;
    using NVOutput = typename JoinAllFuture::StoredOutput;

    return JoinAllDyn{std::move(futs), static_cast<Output*>(nullptr)};
}

}
