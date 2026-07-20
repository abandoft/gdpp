#pragma once

#include "gdpp/semantic/type.hpp"

namespace gdpp {

// Mirrors the two conversion modes used by GDScript: assignment/argument conversion uses Godot's
// strict table, while `as` uses the broader explicit table. Dynamic conversions are accepted by
// the analyzer but must retain a runtime type check/conversion in generated code.
enum class ConversionKind {
    incompatible,
    identity,
    implicit,
    explicit_only,
    dynamic,
};

[[nodiscard]] ConversionKind classify_conversion(const Type& target,
                                                  const Type& source) noexcept;
[[nodiscard]] bool is_implicitly_convertible(const Type& target,
                                             const Type& source) noexcept;
[[nodiscard]] bool is_explicitly_convertible(const Type& target,
                                             const Type& source) noexcept;

} // namespace gdpp
