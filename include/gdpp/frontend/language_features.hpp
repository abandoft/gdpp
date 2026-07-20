#pragma once

#include <cstdint>
#include <string_view>

namespace gdpp {

// Syntax follows the latest supported GDScript language.  Annotation metadata lives in one
// registry so adding an official annotation cannot leave the parser, semantic validation and
// documentation with three independent name lists.
enum class AnnotationTarget : std::uint16_t {
    none = 0,
    script = 1U << 0U,
    field = 1U << 1U,
    function = 1U << 2U,
    signal = 1U << 3U,
    enumeration = 1U << 4U,
    inner_class = 1U << 5U,
    statement = 1U << 6U,
    directive = 1U << 7U,
};

constexpr AnnotationTarget operator|(AnnotationTarget left, AnnotationTarget right) noexcept {
    return static_cast<AnnotationTarget>(static_cast<std::uint16_t>(left) |
                                         static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr bool has_annotation_target(AnnotationTarget targets,
                                                   AnnotationTarget target) noexcept {
    return (static_cast<std::uint16_t>(targets) & static_cast<std::uint16_t>(target)) != 0;
}

enum class AnnotationBehavior : std::uint8_t {
    marker,
    compiler_directive,
    inspector_property,
    inspector_group,
    rpc_configuration,
};

struct AnnotationFeature {
    std::string_view name;
    AnnotationTarget targets{AnnotationTarget::none};
    AnnotationBehavior behavior{AnnotationBehavior::marker};
    std::uint16_t minimum_arguments{0};
    std::uint16_t maximum_arguments{0};
};

class LanguageFeatureRegistry final {
  public:
    [[nodiscard]] static const LanguageFeatureRegistry& latest() noexcept;
    [[nodiscard]] const AnnotationFeature* find_annotation(std::string_view name) const noexcept;
    [[nodiscard]] bool is_warning_name(std::string_view name) const noexcept;

  private:
    LanguageFeatureRegistry() = default;
};

} // namespace gdpp
