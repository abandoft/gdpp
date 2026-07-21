#include "gdpp/runtime/attached_script.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace gdpp::runtime {
namespace {

AttachedCompiledLanguage*& language_singleton() {
    static AttachedCompiledLanguage* value{nullptr};
    return value;
}

godot::TypedArray<godot::Dictionary> method_list(const std::vector<godot::MethodInfo>& methods) {
    godot::TypedArray<godot::Dictionary> result;
    for (const auto& method : methods)
        result.push_back(static_cast<godot::Dictionary>(method));
    return result;
}

const godot::MethodInfo* find_method(const AttachedScriptDescriptor& descriptor,
                                     const godot::StringName& name) {
    const auto found = std::find_if(descriptor.methods.begin(), descriptor.methods.end(),
                                    [&](const auto& item) { return item.name == name; });
    return found == descriptor.methods.end() ? nullptr : &*found;
}

const AttachedScriptProperty* find_property(const AttachedScriptDescriptor& descriptor,
                                            const godot::StringName& name) {
    const auto found = std::find_if(descriptor.properties.begin(), descriptor.properties.end(),
                                    [&](const auto& item) { return item.info.name == name; });
    return found == descriptor.properties.end() ? nullptr : &*found;
}

} // namespace

AttachedCompiledLanguage* AttachedCompiledLanguage::get_singleton() { return language_singleton(); }

bool AttachedCompiledLanguage::register_singleton(godot::String* error) {
    if (language_singleton())
        return true;
    auto* engine = godot::Engine::get_singleton();
    if (!engine) {
        if (error)
            *error = "Godot Engine singleton is unavailable";
        return false;
    }
    auto* language = memnew(AttachedCompiledLanguage);
    const auto status = engine->register_script_language(language);
    if (status != godot::OK) {
        if (error)
            *error = "Godot rejected the GDPP attached script language: " +
                     godot::String::num_int64(status);
        memdelete(language);
        return false;
    }
    language_singleton() = language;
    return true;
}

void AttachedCompiledLanguage::unregister_singleton() {
    auto*& language = language_singleton();
    if (!language)
        return;
    if (auto* engine = godot::Engine::get_singleton()) {
        const auto status = engine->unregister_script_language(language);
        ERR_FAIL_COND_MSG(status != godot::OK,
                          "Godot refused to unregister the GDPP attached script language");
    }
    memdelete(language);
    language = nullptr;
}

godot::String AttachedCompiledLanguage::_get_name() const { return "GDPP AOT"; }
void AttachedCompiledLanguage::_init() {}
godot::String AttachedCompiledLanguage::_get_type() const { return "GDPPCompiledScript"; }
godot::String AttachedCompiledLanguage::_get_extension() const { return "gdppc"; }
void AttachedCompiledLanguage::_finish() {}
godot::PackedStringArray AttachedCompiledLanguage::_get_reserved_words() const { return {}; }
bool AttachedCompiledLanguage::_is_control_flow_keyword(const godot::String&) const {
    return false;
}
godot::PackedStringArray AttachedCompiledLanguage::_get_comment_delimiters() const { return {}; }
godot::PackedStringArray AttachedCompiledLanguage::_get_doc_comment_delimiters() const {
    return {};
}
godot::PackedStringArray AttachedCompiledLanguage::_get_string_delimiters() const { return {}; }

godot::Ref<godot::Script> AttachedCompiledLanguage::_make_template(const godot::String&,
                                                                   const godot::String&,
                                                                   const godot::String&) const {
    return {};
}

godot::TypedArray<godot::Dictionary>
AttachedCompiledLanguage::_get_built_in_templates(const godot::StringName&) const {
    return {};
}

bool AttachedCompiledLanguage::_is_using_templates() { return false; }

godot::Dictionary AttachedCompiledLanguage::_validate(const godot::String&, const godot::String&,
                                                      bool, bool, bool, bool) const {
    godot::Dictionary result;
    result["valid"] = true;
    result["errors"] = godot::Array{};
    result["warnings"] = godot::Array{};
    result["safe_lines"] = godot::Array{};
    return result;
}

godot::String AttachedCompiledLanguage::_validate_path(const godot::String&) const { return {}; }
// This deprecated compatibility callback cannot safely transfer a newly constructed RefCounted
// object through its raw Object* return type on Godot 4.7. Attached scripts are serialized
// AttachedCompiledScript resources and are instantiated through ClassDB, so no runtime path
// depends on this legacy factory.
godot::Object* AttachedCompiledLanguage::_create_script() const { return nullptr; }
bool AttachedCompiledLanguage::_has_named_classes() const { return true; }
bool AttachedCompiledLanguage::_supports_builtin_mode() const { return true; }
bool AttachedCompiledLanguage::_supports_documentation() const { return false; }
bool AttachedCompiledLanguage::_can_inherit_from_file() const { return true; }
std::int32_t AttachedCompiledLanguage::_find_function(const godot::String&,
                                                      const godot::String&) const {
    return -1;
}
godot::String AttachedCompiledLanguage::_make_function(const godot::String&, const godot::String&,
                                                       const godot::PackedStringArray&) const {
    return {};
}
bool AttachedCompiledLanguage::_can_make_function() const { return false; }
godot::Error AttachedCompiledLanguage::_open_in_external_editor(const godot::Ref<godot::Script>&,
                                                                std::int32_t, std::int32_t) {
    return godot::ERR_UNAVAILABLE;
}
bool AttachedCompiledLanguage::_overrides_external_editor() { return false; }
godot::ScriptLanguage::ScriptNameCasing
AttachedCompiledLanguage::_preferred_file_name_casing() const {
    return godot::ScriptLanguage::SCRIPT_NAME_CASING_SNAKE_CASE;
}
godot::Dictionary AttachedCompiledLanguage::_complete_code(const godot::String&,
                                                           const godot::String&,
                                                           godot::Object*) const {
    return {};
}
godot::Dictionary AttachedCompiledLanguage::_lookup_code(const godot::String&, const godot::String&,
                                                         const godot::String&,
                                                         godot::Object*) const {
    return {};
}
godot::String AttachedCompiledLanguage::_auto_indent_code(const godot::String& code, std::int32_t,
                                                          std::int32_t) const {
    return code;
}
void AttachedCompiledLanguage::_add_global_constant(const godot::StringName&,
                                                    const godot::Variant&) {}
void AttachedCompiledLanguage::_add_named_global_constant(const godot::StringName&,
                                                          const godot::Variant&) {}
void AttachedCompiledLanguage::_remove_named_global_constant(const godot::StringName&) {}
void AttachedCompiledLanguage::_thread_enter() {}
void AttachedCompiledLanguage::_thread_exit() {}
godot::String AttachedCompiledLanguage::_debug_get_error() const { return {}; }
std::int32_t AttachedCompiledLanguage::_debug_get_stack_level_count() const { return 0; }
std::int32_t AttachedCompiledLanguage::_debug_get_stack_level_line(std::int32_t) const {
    return -1;
}
godot::String AttachedCompiledLanguage::_debug_get_stack_level_function(std::int32_t) const {
    return {};
}
godot::String AttachedCompiledLanguage::_debug_get_stack_level_source(std::int32_t) const {
    return {};
}
godot::Dictionary
AttachedCompiledLanguage::_debug_get_stack_level_locals(std::int32_t, std::int32_t, std::int32_t) {
    return {};
}
godot::Dictionary
AttachedCompiledLanguage::_debug_get_stack_level_members(std::int32_t, std::int32_t, std::int32_t) {
    return {};
}
void* AttachedCompiledLanguage::_debug_get_stack_level_instance(std::int32_t) { return nullptr; }
godot::Dictionary AttachedCompiledLanguage::_debug_get_globals(std::int32_t, std::int32_t) {
    return {};
}
godot::String AttachedCompiledLanguage::_debug_parse_stack_level_expression(std::int32_t,
                                                                            const godot::String&,
                                                                            std::int32_t,
                                                                            std::int32_t) {
    return {};
}
godot::TypedArray<godot::Dictionary> AttachedCompiledLanguage::_debug_get_current_stack_info() {
    return {};
}
void AttachedCompiledLanguage::_reload_all_scripts() {}
void AttachedCompiledLanguage::_reload_scripts(const godot::Array&, bool) {}
void AttachedCompiledLanguage::_reload_tool_script(const godot::Ref<godot::Script>&, bool) {}
godot::PackedStringArray AttachedCompiledLanguage::_get_recognized_extensions() const {
    godot::PackedStringArray result;
    result.push_back("gdppc");
    return result;
}
godot::TypedArray<godot::Dictionary> AttachedCompiledLanguage::_get_public_functions() const {
    return {};
}
godot::Dictionary AttachedCompiledLanguage::_get_public_constants() const { return {}; }
godot::TypedArray<godot::Dictionary> AttachedCompiledLanguage::_get_public_annotations() const {
    return {};
}
void AttachedCompiledLanguage::_profiling_start() {}
void AttachedCompiledLanguage::_profiling_stop() {}
void AttachedCompiledLanguage::_profiling_set_save_native_calls(bool) {}
std::int32_t AttachedCompiledLanguage::_profiling_get_accumulated_data(
    godot::ScriptLanguageExtensionProfilingInfo*, std::int32_t) {
    return 0;
}
std::int32_t
AttachedCompiledLanguage::_profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo*,
                                                    std::int32_t) {
    return 0;
}
void AttachedCompiledLanguage::_frame() {}

bool AttachedCompiledLanguage::_handles_global_class_type(const godot::String& type) const {
    const auto paths = attached_script_paths();
    return std::any_of(paths.begin(), paths.end(), [&](const auto& path) {
        const auto descriptor = find_attached_script(path);
        return descriptor && descriptor->global_name == type;
    });
}

godot::Dictionary
AttachedCompiledLanguage::_get_global_class_name(const godot::String& path) const {
    const auto descriptor = find_attached_script(path);
    if (!descriptor || descriptor->global_name.is_empty())
        return {};
    godot::Dictionary result;
    result["name"] = descriptor->global_name;
    result["base_type"] = descriptor->native_base_type;
    result["icon_path"] = godot::String{};
    return result;
}

void AttachedCompiledLanguage::_bind_methods() {}

void AttachedCompiledScript::set_source_path(const godot::String& source_path) {
    source_path_ = source_path.simplify_path();
    emit_changed();
}

godot::String AttachedCompiledScript::get_source_path() const { return source_path_; }

std::optional<AttachedScriptDescriptor> AttachedCompiledScript::descriptor() const {
    return resolve_attached_script(source_path_);
}

bool AttachedCompiledScript::_editor_can_reload_from_file() { return false; }

void AttachedCompiledScript::_placeholder_erased(void*) {}

bool AttachedCompiledScript::_can_instantiate() const {
    const auto value = descriptor();
    if (!value || value->abstract || !value->factory)
        return false;

    // Godot rebuilds extension_list.cfg from a HashSet and therefore does not guarantee that a
    // provider GDExtension is initialized before the generated GDPP project extension. Behavior
    // registration is deliberately provider-independent; native availability is resolved here,
    // after every startup extension has entered ClassDB. A missing or disabled provider fails
    // closed instead of advertising a script that the engine cannot instantiate.
    auto* class_db = godot::ClassDBSingleton::get_singleton();
    return class_db && class_db->can_instantiate(value->native_base_type);
}

godot::Ref<godot::Script> AttachedCompiledScript::_get_base_script() const {
    const auto value = descriptor();
    if (!value || value->base_script_path.is_empty())
        return {};
    godot::Ref<AttachedCompiledScript> base;
    base.instantiate();
    base->set_source_path(value->base_script_path);
    return base;
}

godot::StringName AttachedCompiledScript::_get_global_name() const {
    const auto value = descriptor();
    return value ? value->global_name : godot::StringName{};
}

bool AttachedCompiledScript::_inherits_script(const godot::Ref<godot::Script>& script) const {
    const auto* target = godot::Object::cast_to<AttachedCompiledScript>(script.ptr());
    if (!target)
        return false;
    auto current = descriptor();
    while (current && !current->base_script_path.is_empty()) {
        if (current->base_script_path == target->source_path_)
            return true;
        current = find_attached_script(current->base_script_path);
    }
    return false;
}

godot::StringName AttachedCompiledScript::_get_instance_base_type() const {
    const auto value = descriptor();
    return value ? value->native_base_type : godot::StringName{};
}

bool AttachedCompiledScript::_has_source_code() const { return false; }
godot::String AttachedCompiledScript::_get_source_code() const { return {}; }
void AttachedCompiledScript::_set_source_code(const godot::String&) {}
godot::Error AttachedCompiledScript::_reload(bool) {
    return descriptor() ? godot::OK : godot::ERR_FILE_NOT_FOUND;
}

godot::StringName AttachedCompiledScript::_get_doc_class_name() const { return _get_global_name(); }

godot::TypedArray<godot::Dictionary> AttachedCompiledScript::_get_documentation() const {
    return {};
}

godot::String AttachedCompiledScript::_get_class_icon_path() const { return {}; }

bool AttachedCompiledScript::_has_method(const godot::StringName& method) const {
    const auto value = descriptor();
    return value && find_method(*value, method);
}

bool AttachedCompiledScript::_has_static_method(const godot::StringName& method) const {
    const auto value = descriptor();
    const auto* info = value ? find_method(*value, method) : nullptr;
    return info && (info->flags & godot::METHOD_FLAG_STATIC) != 0;
}

godot::Variant
AttachedCompiledScript::_get_script_method_argument_count(const godot::StringName& method) const {
    const auto value = descriptor();
    if (!value)
        return {};
    const auto* info = find_method(*value, method);
    return info ? godot::Variant{static_cast<std::int64_t>(info->arguments.size())}
                : godot::Variant{};
}

godot::Dictionary AttachedCompiledScript::_get_method_info(const godot::StringName& method) const {
    const auto value = descriptor();
    if (!value)
        return {};
    const auto* info = find_method(*value, method);
    return info ? static_cast<godot::Dictionary>(*info) : godot::Dictionary{};
}

bool AttachedCompiledScript::_is_tool() const {
    const auto value = descriptor();
    return value && value->tool;
}
bool AttachedCompiledScript::_is_valid() const { return descriptor().has_value(); }
bool AttachedCompiledScript::_is_abstract() const {
    const auto value = descriptor();
    return value && value->abstract;
}
godot::ScriptLanguage* AttachedCompiledScript::_get_language() const {
    return AttachedCompiledLanguage::get_singleton();
}
bool AttachedCompiledScript::_has_script_signal(const godot::StringName& signal) const {
    const auto value = descriptor();
    return value && std::any_of(value->signals.begin(), value->signals.end(),
                                [&](const auto& item) { return item.name == signal; });
}
godot::TypedArray<godot::Dictionary> AttachedCompiledScript::_get_script_signal_list() const {
    const auto value = descriptor();
    return value ? method_list(value->signals) : godot::TypedArray<godot::Dictionary>{};
}
bool AttachedCompiledScript::_has_property_default_value(const godot::StringName& property) const {
    const auto value = descriptor();
    if (!value)
        return false;
    const auto* info = find_property(*value, property);
    return info && info->has_default;
}
godot::Variant
AttachedCompiledScript::_get_property_default_value(const godot::StringName& property) const {
    const auto value = descriptor();
    if (!value)
        return {};
    const auto* info = find_property(*value, property);
    return info && info->has_default ? info->default_value : godot::Variant{};
}
void AttachedCompiledScript::_update_exports() {}
godot::TypedArray<godot::Dictionary> AttachedCompiledScript::_get_script_method_list() const {
    const auto value = descriptor();
    return value ? method_list(value->methods) : godot::TypedArray<godot::Dictionary>{};
}
godot::TypedArray<godot::Dictionary> AttachedCompiledScript::_get_script_property_list() const {
    godot::TypedArray<godot::Dictionary> result;
    const auto value = descriptor();
    if (!value)
        return result;
    for (const auto& property : value->properties)
        result.push_back(static_cast<godot::Dictionary>(property.info));
    return result;
}
std::int32_t AttachedCompiledScript::_get_member_line(const godot::StringName&) const { return -1; }
godot::Dictionary AttachedCompiledScript::_get_constants() const {
    const auto value = descriptor();
    return value ? value->constants : godot::Dictionary{};
}
godot::TypedArray<godot::StringName> AttachedCompiledScript::_get_members() const {
    godot::TypedArray<godot::StringName> result;
    const auto value = descriptor();
    if (!value)
        return result;
    for (const auto& property : value->properties)
        result.push_back(property.info.name);
    for (const auto& method : value->methods)
        result.push_back(method.name);
    for (const auto& signal : value->signals)
        result.push_back(signal.name);
    const godot::Array constant_names = value->constants.keys();
    for (std::int64_t index = 0; index < constant_names.size(); ++index)
        result.push_back(godot::StringName{constant_names[index]});
    return result;
}
bool AttachedCompiledScript::_is_placeholder_fallback_enabled() const { return false; }
godot::Variant AttachedCompiledScript::_get_rpc_config() const {
    const auto value = descriptor();
    return value ? value->rpc_config : godot::Variant{};
}

void AttachedCompiledScript::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_source_path", "source_path"),
                                &AttachedCompiledScript::set_source_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_source_path"),
                                &AttachedCompiledScript::get_source_path);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "source_path",
                                     godot::PROPERTY_HINT_FILE, "*.gd",
                                     godot::PROPERTY_USAGE_STORAGE),
                 "set_source_path", "get_source_path");
}

} // namespace gdpp::runtime
