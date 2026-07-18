#include "gdpp/frontend/language_features.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace gdpp {
namespace {

constexpr auto annotations = std::array{
    AnnotationFeature{"abstract",
                      AnnotationTarget::script | AnnotationTarget::function |
                          AnnotationTarget::inner_class,
                      AnnotationBehavior::marker, 0, 0},
    AnnotationFeature{"export", AnnotationTarget::field, AnnotationBehavior::inspector_property, 0,
                      0},
    AnnotationFeature{"export_category", AnnotationTarget::field,
                      AnnotationBehavior::inspector_group, 1, 1},
    AnnotationFeature{"export_color_no_alpha", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_custom", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 2, 3},
    AnnotationFeature{"export_dir", AnnotationTarget::field, AnnotationBehavior::inspector_property,
                      0, 0},
    AnnotationFeature{"export_enum", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 1,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_exp_easing", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_file", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_file_path", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_flags", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 1,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_flags_2d_navigation", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_2d_physics", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_2d_render", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_3d_navigation", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_3d_physics", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_3d_render", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_flags_avoidance", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_global_dir", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_global_file", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_group", AnnotationTarget::field, AnnotationBehavior::inspector_group,
                      1, 2},
    AnnotationFeature{"export_multiline", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_node_path", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_placeholder", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 1, 1},
    AnnotationFeature{"export_range", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 2,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"export_storage", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 0, 0},
    AnnotationFeature{"export_subgroup", AnnotationTarget::field,
                      AnnotationBehavior::inspector_group, 1, 2},
    AnnotationFeature{"export_tool_button", AnnotationTarget::field,
                      AnnotationBehavior::inspector_property, 1, 2},
    AnnotationFeature{"icon", AnnotationTarget::script, AnnotationBehavior::marker, 1, 1},
    AnnotationFeature{"onready", AnnotationTarget::field, AnnotationBehavior::marker, 0, 0},
    AnnotationFeature{"rpc", AnnotationTarget::function, AnnotationBehavior::rpc_configuration, 0,
                      4},
    AnnotationFeature{"static_unload", AnnotationTarget::script, AnnotationBehavior::marker, 0, 0},
    AnnotationFeature{"tool", AnnotationTarget::script, AnnotationBehavior::marker, 0, 0},
    AnnotationFeature{"warning_ignore",
                      AnnotationTarget::field | AnnotationTarget::function |
                          AnnotationTarget::signal | AnnotationTarget::enumeration |
                          AnnotationTarget::inner_class | AnnotationTarget::statement,
                      AnnotationBehavior::compiler_directive, 1,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"warning_ignore_restore", AnnotationTarget::directive,
                      AnnotationBehavior::compiler_directive, 1,
                      std::numeric_limits<std::uint16_t>::max()},
    AnnotationFeature{"warning_ignore_start", AnnotationTarget::directive,
                      AnnotationBehavior::compiler_directive, 1,
                      std::numeric_limits<std::uint16_t>::max()},
};

static_assert(
    [] {
        for (std::size_t index = 1; index < annotations.size(); ++index) {
            if (annotations[index - 1].name >= annotations[index].name)
                return false;
        }
        return true;
    }(),
    "annotation registry must remain sorted");

} // namespace

const LanguageFeatureRegistry& LanguageFeatureRegistry::latest() noexcept {
    static const LanguageFeatureRegistry registry;
    return registry;
}

const AnnotationFeature*
LanguageFeatureRegistry::find_annotation(const std::string_view name) const noexcept {
    const auto found =
        std::lower_bound(annotations.begin(), annotations.end(), name,
                         [](const AnnotationFeature& feature, const std::string_view candidate) {
                             return feature.name < candidate;
                         });
    return found != annotations.end() && found->name == name ? &*found : nullptr;
}

} // namespace gdpp
