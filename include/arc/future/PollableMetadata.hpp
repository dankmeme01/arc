#pragma once
#include <arc/util/Typename.hpp>
namespace arc {

struct PollableMetadata {
    std::string_view typeName;
    bool isFuture = false;

    template <typename T, bool IsFuture = false>
    static constexpr const PollableMetadata* create() {
        static constexpr auto value = [] {
            return PollableMetadata{
                .typeName = getTypename<T>(),
                .isFuture = IsFuture,
            };
        }();
        return &value;
    }
};

}
