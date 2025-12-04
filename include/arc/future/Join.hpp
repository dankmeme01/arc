#pragma once

#include "Future.hpp"
#include <arc/util/Assert.hpp>

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

    Future<Output> future;
    std::optional<StoredOutput> output;
};

template <typename FRet, typename... Futures>
struct JoinAll : PollableBase<JoinAll<Futures...>, std::array<FRet, sizeof...(Futures)>> {
    using Output = std::array<FRet, sizeof...(Futures)>;
    explicit JoinAll(std::tuple<Futures...>&& futs, FRet*) : m_futures(std::move(futs)) {}

    std::optional<Output> poll() {
        bool allDone = true;
        this->checkForEach(*m_futures, allDone);

        if (!allDone) {
            return std::nullopt;
        }

        Output out;
        this->extractForEach(*m_futures, out);

        return std::make_optional<Output>(std::move(out));
    }

    template <typename Tuple>
    void checkForEach(Tuple&& t, bool& allDone) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        checkForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, allDone);
    }

    template <size_t... Is, typename Tuple>
    void checkForEachImpl(Tuple&& t, std::index_sequence<Is...>, bool& allDone) {
        (([&]() {
            auto& fut = std::get<Is>(t);

            trace("[JoinAll] checking future {}, active: {}", Is, !fut.output);
            if (!fut.output) {
                allDone = false;
                auto res = fut.future.poll();
                if (res) {
                    if constexpr (fut.IsVoid) {
                        fut.output = std::monostate{};
                    } else {
                        fut.output = std::move(res).value();
                    }

                    trace("[JoinAll] future {} finished!", Is);
                }
            }
        }()), ...);
    }

    template <typename Tuple>
    void extractForEach(Tuple&& t, Output& out) {
        constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
        extractForEachImpl(std::forward<Tuple>(t), std::make_index_sequence<size>{}, out);
    }

    template <size_t... Is, typename Tuple>
    void extractForEachImpl(Tuple&& t, std::index_sequence<Is...>, Output& out) {
        (([&]() {
            auto& fut = std::get<Is>(t);

            if constexpr (!fut.IsVoid) {
                ARC_DEBUG_ASSERT(fut.output.has_value());
                out[Is] = std::move(fut.output).value();
            }
        }()), ...);
    }

private:
    std::optional<std::tuple<Futures...>> m_futures;
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
    JoinAll joinAll{std::move(fs), static_cast<NVOutput*>(nullptr)};
    return joinAll;
}


}