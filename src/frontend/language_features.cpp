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

// Godot 4.7's complete GDScriptWarning::Code name table. Keep source spelling lower-case because
// annotation arguments use the ProjectSettings warning keys, not C++ enum identifiers.
constexpr auto warning_names = std::array{
    std::string_view{"unassigned_variable"},
    std::string_view{"unassigned_variable_op_assign"},
    std::string_view{"unused_variable"},
    std::string_view{"unused_local_constant"},
    std::string_view{"unused_private_class_variable"},
    std::string_view{"unused_parameter"},
    std::string_view{"unused_signal"},
    std::string_view{"shadowed_variable"},
    std::string_view{"shadowed_variable_base_class"},
    std::string_view{"shadowed_global_identifier"},
    std::string_view{"unreachable_code"},
    std::string_view{"unreachable_pattern"},
    std::string_view{"standalone_expression"},
    std::string_view{"standalone_ternary"},
    std::string_view{"incompatible_ternary"},
    std::string_view{"untyped_declaration"},
    std::string_view{"inferred_declaration"},
    std::string_view{"unsafe_property_access"},
    std::string_view{"unsafe_method_access"},
    std::string_view{"unsafe_cast"},
    std::string_view{"unsafe_call_argument"},
    std::string_view{"unsafe_void_return"},
    std::string_view{"return_value_discarded"},
    std::string_view{"static_called_on_instance"},
    std::string_view{"missing_tool"},
    std::string_view{"redundant_static_unload"},
    std::string_view{"redundant_await"},
    std::string_view{"missing_await"},
    std::string_view{"assert_always_true"},
    std::string_view{"assert_always_false"},
    std::string_view{"integer_division"},
    std::string_view{"narrowing_conversion"},
    std::string_view{"int_as_enum_without_cast"},
    std::string_view{"int_as_enum_without_match"},
    std::string_view{"enum_variable_without_default"},
    std::string_view{"empty_file"},
    std::string_view{"deprecated_keyword"},
    std::string_view{"confusable_identifier"},
    std::string_view{"confusable_local_declaration"},
    std::string_view{"confusable_local_usage"},
    std::string_view{"confusable_capture_reassignment"},
    std::string_view{"confusable_temporary_modification"},
    std::string_view{"inference_on_variant"},
    std::string_view{"native_method_override"},
    std::string_view{"get_node_default_without_onready"},
    std::string_view{"onready_with_export"},
    std::string_view{"property_used_as_function"},
    std::string_view{"constant_used_as_function"},
    std::string_view{"function_used_as_property"},
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

bool LanguageFeatureRegistry::is_warning_name(const std::string_view name) const noexcept {
    return std::find(warning_names.begin(), warning_names.end(), name) != warning_names.end();
}

} // namespace gdpp
