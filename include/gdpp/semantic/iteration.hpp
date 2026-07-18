#pragma once

#include "gdpp/semantic/type.hpp"

#include <cstdint>
#include <string_view>

namespace gdpp {

// Iteration is a semantic contract, not a backend guess. In particular, Dictionary iteration
// yields keys, String iteration yields one-codepoint strings, and mutable Array/PackedArray loops
// must observe the same live storage as GDScript.
enum class IterationStrategy : std::uint8_t {
    none,
    dynamic_protocol,
    integer_count,
    intrinsic_range,
    indexed_string,
    indexed_array,
    indexed_packed_array,
    dictionary_protocol,
};

struct IterationPlan {
    IterationStrategy strategy{IterationStrategy::none};
    Type element_type;

    [[nodiscard]] bool valid() const noexcept {
        return strategy != IterationStrategy::none && element_type.kind != TypeKind::unknown;
    }
};

[[nodiscard]] IterationPlan make_iteration_plan(const Type& iterable, const Type& element_type,
                                                bool intrinsic_range);
[[nodiscard]] std::string_view iteration_strategy_name(IterationStrategy strategy) noexcept;

} // namespace gdpp
