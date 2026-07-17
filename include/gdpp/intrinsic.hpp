#pragma once

#include <cstdint>
#include <string_view>

namespace gdpp {

// Language intrinsics are not Godot API utility functions: their typing/lowering remains stable
// across target engine API tables and may require compiler-specific treatment.
enum class IntrinsicKind : std::uint8_t { none, load, preload, range, length };

enum class IntrinsicArgumentRule : std::uint8_t { any, integer, resource_path };
enum class IntrinsicResultRule : std::uint8_t { dynamic, integer, integer_array, resource };

struct IntrinsicFeature {
    IntrinsicKind kind{IntrinsicKind::none};
    std::string_view name;
    std::uint8_t minimum_arguments{0};
    std::uint8_t maximum_arguments{0};
    IntrinsicArgumentRule argument_rule{IntrinsicArgumentRule::any};
    IntrinsicResultRule result_rule{IntrinsicResultRule::dynamic};
    std::string_view runtime_symbol;
};

class IntrinsicRegistry final {
  public:
    [[nodiscard]] static const IntrinsicRegistry& latest() noexcept;
    [[nodiscard]] const IntrinsicFeature* find(std::string_view name) const noexcept;
    [[nodiscard]] const IntrinsicFeature* find(IntrinsicKind kind) const noexcept;

  private:
    IntrinsicRegistry() = default;
};

} // namespace gdpp
