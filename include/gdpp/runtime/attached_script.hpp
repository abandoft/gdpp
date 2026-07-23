#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
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

// RefCounted construction changed in godot-cpp 4.7: memnew(T) now returns Ref<T> instead of a
// raw pointer. Keep the factory ownership explicit so generated code has one contract on every
// supported Godot release and never depends on the include order of ref.hpp.
using AttachedBehaviorFactory = godot::Ref<AttachedScriptBehavior> (*)();

struct AttachedScriptProperty {
    godot::PropertyInfo info;
    godot::Variant default_value;
    bool has_default{false};
};

struct AttachedScriptDescriptor {
    AttachedScriptDescriptor() = default;
    AttachedScriptDescriptor(const AttachedScriptDescriptor&) = default;
    AttachedScriptDescriptor(AttachedScriptDescriptor&&) = default;
    AttachedScriptDescriptor& operator=(const AttachedScriptDescriptor& other);
    AttachedScriptDescriptor& operator=(AttachedScriptDescriptor&& other);

    godot::String source_path;
    godot::StringName global_name;
    godot::StringName native_base_type;
    godot::String base_script_path;
    godot::String contract_hash;
    godot::StringName behavior_class;
    AttachedBehaviorFactory factory{nullptr};
    std::vector<AttachedScriptProperty> properties;
    std::vector<godot::MethodInfo> methods;
    std::vector<godot::MethodInfo> signals;
    godot::Dictionary constants;
    godot::Variant rpc_config;
    bool tool{false};
    bool abstract{false};
    // The compiler extension installs reflection-only descriptors while export resources are
    // rewritten. They deliberately have no behavior factory: the editor needs property storage
    // and Script reflection, while executable behavior exists only in the target library.
    bool editor_metadata_only{false};
};

// Registration is deterministic and fails closed: duplicate paths are accepted only when their
// externally visible identity is identical. Generated libraries register every descriptor after
// all behavior classes have entered ClassDB.
[[nodiscard]] bool register_attached_script(AttachedScriptDescriptor descriptor,
                                            godot::String* error = nullptr);
void unregister_all_attached_scripts();
[[nodiscard]] std::optional<AttachedScriptDescriptor>
find_attached_script(const godot::String& source_path);
// Resolves a complete script view, with derived declarations shadowing inherited ones. A missing
// or cyclic base chain fails closed and returns no descriptor.
[[nodiscard]] std::optional<AttachedScriptDescriptor>
resolve_attached_script(const godot::String& source_path, godot::String* error = nullptr);
[[nodiscard]] std::vector<godot::String> attached_script_paths();

// Script types are identities attached to a Godot owner, not ClassDB subclasses of that owner.
// These helpers provide the runtime equivalent of GDScript's `is` and `as` operations without
// ever casting an owner pointer to a generated behavior implementation.
[[nodiscard]] bool is_attached_script_instance(godot::Object* object,
                                               const godot::String& source_path);
// Restricts export-time ScriptInstance property-state serialization to fields that were actually
// stored by the source scene/resource. Target runtime instances reject this metadata-only API.
[[nodiscard]] bool set_attached_editor_storage_state(
    godot::Object* object, const godot::PackedStringArray& stored_properties);
[[nodiscard]] godot::Object* cast_attached_script(const godot::Variant& value,
                                                  const godot::String& source_path);

// Constructs the provider-owned native object, attaches the compiled script and invokes _init
// with the supplied arguments. The Variant preserves RefCounted ownership when applicable.
[[nodiscard]] godot::Variant instantiate_attached_script(const godot::String& source_path,
                                                         const godot::Array& arguments = {},
                                                         godot::String* error = nullptr);

[[nodiscard]] godot::Variant
call_attached_native_base_raw(godot::Object* owner, const godot::StringName& native_class,
                              const godot::StringName& method, std::uint32_t compatibility_hash,
                              const godot::Variant** arguments, std::int64_t argument_count);

template <typename... Arguments>
[[nodiscard]] godot::Variant
call_attached_native_base(godot::Object* owner, const godot::StringName& native_class,
                          const godot::StringName& method, std::uint32_t compatibility_hash,
                          Arguments&&... arguments) {
    std::array<godot::Variant, sizeof...(Arguments)> values{
        godot::Variant(std::forward<Arguments>(arguments))...};
    std::array<const godot::Variant*, sizeof...(Arguments)> pointers{};
    for (std::size_t index = 0; index < values.size(); ++index)
        pointers[index] = &values[index];
    return call_attached_native_base_raw(owner, native_class, method, compatibility_hash,
                                         pointers.data(),
                                         static_cast<std::int64_t>(pointers.size()));
}

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
    godot::Error _open_in_external_editor(const godot::Ref<godot::Script>& script,
                                          std::int32_t line, std::int32_t column) override;
    bool _overrides_external_editor() override;
    godot::ScriptLanguage::ScriptNameCasing _preferred_file_name_casing() const override;
    godot::Dictionary _complete_code(const godot::String& code, const godot::String& path,
                                     godot::Object* owner) const override;
    godot::Dictionary _lookup_code(const godot::String& code, const godot::String& symbol,
                                   const godot::String& path, godot::Object* owner) const override;
    godot::String _auto_indent_code(const godot::String& code, std::int32_t from_line,
                                    std::int32_t to_line) const override;
    void _add_global_constant(const godot::StringName& name, const godot::Variant& value) override;
    void _add_named_global_constant(const godot::StringName& name,
                                    const godot::Variant& value) override;
    void _remove_named_global_constant(const godot::StringName& name) override;
    void _thread_enter() override;
    void _thread_exit() override;
    godot::String _debug_get_error() const override;
    std::int32_t _debug_get_stack_level_count() const override;
    std::int32_t _debug_get_stack_level_line(std::int32_t level) const override;
    godot::String _debug_get_stack_level_function(std::int32_t level) const override;
    godot::String _debug_get_stack_level_source(std::int32_t level) const override;
    godot::Dictionary _debug_get_stack_level_locals(std::int32_t level, std::int32_t max_subitems,
                                                    std::int32_t max_depth) override;
    godot::Dictionary _debug_get_stack_level_members(std::int32_t level, std::int32_t max_subitems,
                                                     std::int32_t max_depth) override;
    void* _debug_get_stack_level_instance(std::int32_t level) override;
    godot::Dictionary _debug_get_globals(std::int32_t max_subitems,
                                         std::int32_t max_depth) override;
    godot::String _debug_parse_stack_level_expression(std::int32_t level,
                                                      const godot::String& expression,
                                                      std::int32_t max_subitems,
                                                      std::int32_t max_depth) override;
    godot::TypedArray<godot::Dictionary> _debug_get_current_stack_info() override;
    void _reload_all_scripts() override;
    void _reload_scripts(const godot::Array& scripts, bool soft_reload) override;
    void _reload_tool_script(const godot::Ref<godot::Script>& script, bool soft_reload) override;
    godot::PackedStringArray _get_recognized_extensions() const override;
    godot::TypedArray<godot::Dictionary> _get_public_functions() const override;
    godot::Dictionary _get_public_constants() const override;
    godot::TypedArray<godot::Dictionary> _get_public_annotations() const override;
    void _profiling_start() override;
    void _profiling_stop() override;
    void _profiling_set_save_native_calls(bool enable) override;
    std::int32_t
    _profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo* info_array,
                                    std::int32_t info_max) override;
    std::int32_t _profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo* info_array,
                                           std::int32_t info_max) override;
    void _frame() override;
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
    void set_contract_hash(const godot::String& contract_hash);
    [[nodiscard]] godot::String get_contract_hash() const;

    bool _editor_can_reload_from_file() override;
    void _placeholder_erased(void* placeholder) override;
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
    godot::StringName _get_doc_class_name() const override;
    godot::TypedArray<godot::Dictionary> _get_documentation() const override;
    godot::String _get_class_icon_path() const override;
    bool _has_method(const godot::StringName& method) const override;
    bool _has_static_method(const godot::StringName& method) const override;
    godot::Variant
    _get_script_method_argument_count(const godot::StringName& method) const override;
    godot::Dictionary _get_method_info(const godot::StringName& method) const override;
    bool _is_tool() const override;
    bool _is_valid() const override;
    bool _is_abstract() const override;
    godot::ScriptLanguage* _get_language() const override;
    bool _has_script_signal(const godot::StringName& signal) const override;
    godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
    bool _has_property_default_value(const godot::StringName& property) const override;
    godot::Variant _get_property_default_value(const godot::StringName& property) const override;
    void _update_exports() override;
    godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
    godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
    std::int32_t _get_member_line(const godot::StringName& member) const override;
    godot::Dictionary _get_constants() const override;
    godot::TypedArray<godot::StringName> _get_members() const override;
    bool _is_placeholder_fallback_enabled() const override;
    godot::Variant _get_rpc_config() const override;

  protected:
    static void _bind_methods();

  private:
    [[nodiscard]] std::optional<AttachedScriptDescriptor> descriptor() const;

    godot::String source_path_;
    godot::String contract_hash_;
};

} // namespace gdpp::runtime
