#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace gdpp::runtime {

// The generated behavior object owns script fields and methods. The Godot object remains an
// instance of its original ClassDB type, including when that type belongs to another
// GDExtension. Keeping the owner as a non-owning pointer avoids a reference cycle: Godot owns the
// ScriptInstance, and the ScriptInstance owns the behavior.
class AttachedScriptBehavior : public godot::RefCounted {
    GDCLASS(AttachedScriptBehavior, godot::RefCounted)

  public:
    void attach_owner(godot::Object* owner);
    void detach_owner();
    [[nodiscard]] godot::Object* owner() const;

    // Generated classes override these hooks so initialization runs only after the external
    // owner is attached. This is required for field initializers that access self or native
    // properties.
    virtual void initialize_instance();
    virtual void dispatch_notification(std::int32_t what, bool reversed);

  protected:
    static void _bind_methods();

  private:
    godot::Object* owner_{nullptr};
};

using AttachedBehaviorFactory = AttachedScriptBehavior* (*)();

struct AttachedScriptProperty {
    godot::PropertyInfo info;
    godot::Variant default_value;
    bool has_default{false};
};

struct AttachedScriptDescriptor {
    godot::String source_path;
    godot::StringName global_name;
    godot::StringName native_base_type;
    godot::String base_script_path;
    godot::StringName behavior_class;
    AttachedBehaviorFactory factory{nullptr};
    std::vector<AttachedScriptProperty> properties;
    std::vector<godot::MethodInfo> methods;
    std::vector<godot::MethodInfo> signals;
    godot::Dictionary constants;
    godot::Variant rpc_config;
    bool tool{false};
    bool abstract{false};
};

// Registration is deterministic and fails closed: duplicate paths are accepted only when their
// externally visible identity is identical. Generated libraries register every descriptor after
// all behavior classes have entered ClassDB.
[[nodiscard]] bool register_attached_script(AttachedScriptDescriptor descriptor,
                                            godot::String* error = nullptr);
void unregister_all_attached_scripts();
[[nodiscard]] std::optional<AttachedScriptDescriptor>
find_attached_script(const godot::String& source_path);
[[nodiscard]] std::vector<godot::String> attached_script_paths();

class AttachedCompiledScript;

class AttachedCompiledLanguage : public godot::ScriptLanguageExtension {
    GDCLASS(AttachedCompiledLanguage, godot::ScriptLanguageExtension)

  public:
    static AttachedCompiledLanguage* get_singleton();
    static bool register_singleton(godot::String* error = nullptr);
    static void unregister_singleton();

    godot::String _get_name() const override;
    void _init() override;
    godot::String _get_type() const override;
    godot::String _get_extension() const override;
    void _finish() override;
    godot::PackedStringArray _get_reserved_words() const override;
    bool _is_control_flow_keyword(const godot::String& keyword) const override;
    godot::PackedStringArray _get_comment_delimiters() const override;
    godot::PackedStringArray _get_doc_comment_delimiters() const override;
    godot::PackedStringArray _get_string_delimiters() const override;
    godot::Ref<godot::Script> _make_template(const godot::String& source,
                                              const godot::String& class_name,
                                              const godot::String& base_class_name) const override;
    godot::TypedArray<godot::Dictionary>
    _get_built_in_templates(const godot::StringName& object) const override;
    bool _is_using_templates() override;
    godot::Dictionary _validate(const godot::String& script, const godot::String& path,
                                bool validate_functions, bool validate_errors,
                                bool validate_warnings, bool validate_safe_lines) const override;
    godot::String _validate_path(const godot::String& path) const override;
    godot::Object* _create_script() const override;
    bool _has_named_classes() const override;
    bool _supports_builtin_mode() const override;
    bool _supports_documentation() const override;
    bool _can_inherit_from_file() const override;
    std::int32_t _find_function(const godot::String& function,
                                const godot::String& code) const override;
    godot::String _make_function(const godot::String& class_name,
                                 const godot::String& function_name,
                                 const godot::PackedStringArray& function_args) const override;
    bool _can_make_function() const override;
    godot::PackedStringArray _get_recognized_extensions() const override;
    godot::TypedArray<godot::Dictionary> _get_public_functions() const override;
    godot::Dictionary _get_public_constants() const override;
    godot::TypedArray<godot::Dictionary> _get_public_annotations() const override;
    bool _handles_global_class_type(const godot::String& type) const override;
    godot::Dictionary _get_global_class_name(const godot::String& path) const override;

  protected:
    static void _bind_methods();
};

class AttachedCompiledScript : public godot::ScriptExtension {
    GDCLASS(AttachedCompiledScript, godot::ScriptExtension)

  public:
    void set_source_path(const godot::String& source_path);
    [[nodiscard]] godot::String get_source_path() const;

    bool _editor_can_reload_from_file() override;
    bool _can_instantiate() const override;
    godot::Ref<godot::Script> _get_base_script() const override;
    godot::StringName _get_global_name() const override;
    bool _inherits_script(const godot::Ref<godot::Script>& script) const override;
    godot::StringName _get_instance_base_type() const override;
    void* _instance_create(godot::Object* object) const override;
    void* _placeholder_instance_create(godot::Object* object) const override;
    bool _instance_has(godot::Object* object) const override;
    bool _has_source_code() const override;
    godot::String _get_source_code() const override;
    void _set_source_code(const godot::String& code) override;
    godot::Error _reload(bool keep_state) override;
    bool _has_method(const godot::StringName& method) const override;
    godot::Variant _get_script_method_argument_count(const godot::StringName& method) const override;
    godot::Dictionary _get_method_info(const godot::StringName& method) const override;
    bool _is_tool() const override;
    bool _is_valid() const override;
    bool _is_abstract() const override;
    godot::ScriptLanguage* _get_language() const override;
    bool _has_script_signal(const godot::StringName& signal) const override;
    godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
    bool _has_property_default_value(const godot::StringName& property) const override;
    godot::Variant _get_property_default_value(const godot::StringName& property) const override;
    godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
    godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
    godot::Dictionary _get_constants() const override;
    godot::TypedArray<godot::StringName> _get_members() const override;
    godot::Variant _get_rpc_config() const override;

  protected:
    static void _bind_methods();

  private:
    [[nodiscard]] std::optional<AttachedScriptDescriptor> descriptor() const;

    godot::String source_path_;
};

} // namespace gdpp::runtime
