#include "gdpp/runtime/attached_script.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <gdextension_interface.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gdpp::runtime {
namespace {

class AttachedScriptInstance final {
  public:
    AttachedScriptInstance(godot::Object* owner, godot::Ref<AttachedCompiledScript> script,
                           AttachedScriptDescriptor descriptor,
                           godot::Ref<AttachedScriptBehavior> behavior)
        : owner{owner}, script{std::move(script)}, descriptor{std::move(descriptor)},
          behavior{std::move(behavior)} {}

    ~AttachedScriptInstance() {
        behavior->detach_owner();
        std::lock_guard<std::mutex> lock{instances_mutex()};
        instances().erase(owner);
    }

    static std::mutex& instances_mutex() {
        static std::mutex value;
        return value;
    }

    static std::unordered_map<godot::Object*, AttachedScriptInstance*>& instances() {
        static std::unordered_map<godot::Object*, AttachedScriptInstance*> value;
        return value;
    }

    godot::Object* owner;
    godot::Ref<AttachedCompiledScript> script;
    AttachedScriptDescriptor descriptor;
    godot::Ref<AttachedScriptBehavior> behavior;
};

struct PendingConstruction {
    godot::String source_path;
    const godot::Array* arguments{nullptr};
    bool consumed{false};
};

thread_local PendingConstruction* pending_construction{nullptr};

std::mutex& native_bind_mutex() {
    static std::mutex value;
    return value;
}

std::unordered_map<std::string, GDExtensionMethodBindPtr>& native_bind_cache() {
    static std::unordered_map<std::string, GDExtensionMethodBindPtr> value;
    return value;
}

std::string native_bind_key(const godot::StringName& native_class, const godot::StringName& method,
                            const std::uint32_t compatibility_hash) {
    const godot::CharString class_utf8 = godot::String{native_class}.utf8();
    const godot::CharString method_utf8 = godot::String{method}.utf8();
    return std::string{class_utf8.get_data(), static_cast<std::size_t>(class_utf8.length())} +
           "\n" +
           std::string{method_utf8.get_data(), static_cast<std::size_t>(method_utf8.length())} +
           "\n" + std::to_string(compatibility_hash);
}

const AttachedScriptProperty* find_property(const AttachedScriptInstance* instance,
                                            const godot::StringName& name) {
    const auto found =
        std::find_if(instance->descriptor.properties.begin(), instance->descriptor.properties.end(),
                     [&](const auto& item) { return item.info.name == name; });
    return found == instance->descriptor.properties.end() ? nullptr : &*found;
}

const godot::MethodInfo* find_method(const AttachedScriptInstance* instance,
                                     const godot::StringName& name) {
    const auto found =
        std::find_if(instance->descriptor.methods.begin(), instance->descriptor.methods.end(),
                     [&](const auto& item) { return item.name == name; });
    return found == instance->descriptor.methods.end() ? nullptr : &*found;
}

const godot::MethodInfo* find_method(const AttachedScriptDescriptor& descriptor,
                                     const godot::StringName& name) {
    const auto found = std::find_if(descriptor.methods.begin(), descriptor.methods.end(),
                                    [&](const auto& item) { return item.name == name; });
    return found == descriptor.methods.end() ? nullptr : &*found;
}

bool has_signal(const AttachedScriptInstance* instance, const godot::StringName& name) {
    return std::any_of(instance->descriptor.signals.begin(), instance->descriptor.signals.end(),
                       [&](const auto& item) { return item.name == name; });
}

godot::Variant call_behavior(AttachedScriptInstance* instance, const godot::StringName& method,
                             const godot::Variant** arguments, std::int64_t argument_count,
                             GDExtensionCallError& error) {
    godot::Variant target{instance->behavior.ptr()};
    godot::Variant result;
    target.callp(method, arguments, static_cast<int>(argument_count), result, error);
    return result;
}

GDExtensionVariantPtr copy_variant(const godot::Variant& value) {
    return memnew(godot::Variant(value));
}

void destroy_variant(GDExtensionVariantPtr value) {
    memdelete(reinterpret_cast<godot::Variant*>(value));
}

GDExtensionPropertyInfo copy_property_info(const godot::PropertyInfo& info) {
    return {static_cast<GDExtensionVariantType>(info.type), memnew(godot::StringName(info.name)),
            memnew(godot::StringName(info.class_name)),     info.hint,
            memnew(godot::String(info.hint_string)),        info.usage};
}

void destroy_property_info(const GDExtensionPropertyInfo& info) {
    memdelete(reinterpret_cast<godot::StringName*>(info.name));
    memdelete(reinterpret_cast<godot::StringName*>(info.class_name));
    memdelete(reinterpret_cast<godot::String*>(info.hint_string));
}

void destroy_property_infos(const GDExtensionPropertyInfo* values, std::uint32_t count) {
    if (!values)
        return;
    for (std::uint32_t index = 0; index < count; ++index)
        destroy_property_info(values[index]);
    godot::memdelete_arr(values);
}

GDExtensionMethodInfo copy_method_info(const godot::MethodInfo& info) {
    auto* arguments = info.arguments.is_empty()
                          ? nullptr
                          : memnew_arr(GDExtensionPropertyInfo, info.arguments.size());
    for (std::uint32_t index = 0; index < info.arguments.size(); ++index)
        arguments[index] = copy_property_info(info.arguments[index]);

    auto** defaults = info.default_arguments.is_empty()
                          ? nullptr
                          : memnew_arr(GDExtensionVariantPtr, info.default_arguments.size());
    for (std::uint32_t index = 0; index < info.default_arguments.size(); ++index)
        defaults[index] = copy_variant(info.default_arguments[index]);

    return {memnew(godot::StringName(info.name)),
            copy_property_info(info.return_val),
            info.flags,
            info.id,
            info.arguments.size(),
            arguments,
            info.default_arguments.size(),
            defaults};
}

void destroy_method_info(const GDExtensionMethodInfo& info) {
    memdelete(reinterpret_cast<godot::StringName*>(info.name));
    destroy_property_info(info.return_value);
    destroy_property_infos(info.arguments, info.argument_count);
    if (info.default_arguments) {
        for (std::uint32_t index = 0; index < info.default_argument_count; ++index)
            destroy_variant(info.default_arguments[index]);
        godot::memdelete_arr(info.default_arguments);
    }
}

void destroy_method_infos(const GDExtensionMethodInfo* values, std::uint32_t count) {
    if (!values)
        return;
    for (std::uint32_t index = 0; index < count; ++index)
        destroy_method_info(values[index]);
    godot::memdelete_arr(values);
}

GDExtensionBool instance_set(AttachedScriptInstance* instance, const godot::StringName* name,
                             const godot::Variant* value) {
    if (find_method(instance, "_set")) {
        const godot::Variant property_name{*name};
        const godot::Variant* arguments[]{&property_name, value};
        GDExtensionCallError error{};
        const auto handled = call_behavior(instance, "_set", arguments, 2, error);
        if (error.error == GDEXTENSION_CALL_OK && static_cast<bool>(handled))
            return true;
    }
    if (!find_property(instance, *name))
        return false;
    return godot::ClassDB::class_set_property(instance->behavior.ptr(), *name, *value) == godot::OK;
}

GDExtensionBool instance_get(AttachedScriptInstance* instance, const godot::StringName* name,
                             godot::Variant* result) {
    if (find_method(instance, "_get")) {
        const godot::Variant property_name{*name};
        const godot::Variant* arguments[]{&property_name};
        GDExtensionCallError error{};
        const auto value = call_behavior(instance, "_get", arguments, 1, error);
        if (error.error == GDEXTENSION_CALL_OK && value.get_type() != godot::Variant::NIL) {
            *result = value;
            return true;
        }
    }
    if (find_property(instance, *name)) {
        *result = godot::ClassDB::class_get_property(instance->behavior.ptr(), *name);
        return true;
    }
    if (find_method(instance, *name)) {
        *result = godot::Callable(instance->owner, *name);
        return true;
    }
    if (has_signal(instance, *name)) {
        *result = godot::Signal(instance->owner, *name);
        return true;
    }
    return false;
}

const GDExtensionPropertyInfo* instance_property_list(AttachedScriptInstance* instance,
                                                      std::uint32_t* count) {
    *count = static_cast<std::uint32_t>(instance->descriptor.properties.size());
    if (*count == 0)
        return nullptr;
    auto* result = memnew_arr(GDExtensionPropertyInfo, *count);
    for (std::uint32_t index = 0; index < *count; ++index)
        result[index] = copy_property_info(instance->descriptor.properties[index].info);
    return result;
}

void instance_free_property_list(AttachedScriptInstance*, const GDExtensionPropertyInfo* values,
                                 std::uint32_t count) {
    destroy_property_infos(values, count);
}

GDExtensionBool instance_property_can_revert(AttachedScriptInstance* instance,
                                             const godot::StringName* name) {
    const auto* property = find_property(instance, *name);
    return property && property->has_default;
}

GDExtensionBool instance_property_get_revert(AttachedScriptInstance* instance,
                                             const godot::StringName* name,
                                             godot::Variant* result) {
    const auto* property = find_property(instance, *name);
    if (!property || !property->has_default)
        return false;
    *result = property->default_value;
    return true;
}

GDExtensionObjectPtr instance_owner(AttachedScriptInstance* instance) {
    return instance->owner->_owner;
}

void instance_property_state(AttachedScriptInstance* instance,
                             GDExtensionScriptInstancePropertyStateAdd add, void* userdata) {
    for (const auto& property : instance->descriptor.properties) {
        if ((property.info.usage & godot::PROPERTY_USAGE_STORAGE) == 0)
            continue;
        godot::Variant value =
            godot::ClassDB::class_get_property(instance->behavior.ptr(), property.info.name);
        add(&property.info.name, &value, userdata);
    }
}

const GDExtensionMethodInfo* instance_method_list(AttachedScriptInstance* instance,
                                                  std::uint32_t* count) {
    *count = static_cast<std::uint32_t>(instance->descriptor.methods.size());
    if (*count == 0)
        return nullptr;
    auto* result = memnew_arr(GDExtensionMethodInfo, *count);
    for (std::uint32_t index = 0; index < *count; ++index)
        result[index] = copy_method_info(instance->descriptor.methods[index]);
    return result;
}

void instance_free_method_list(AttachedScriptInstance*, const GDExtensionMethodInfo* values,
                               std::uint32_t count) {
    destroy_method_infos(values, count);
}

GDExtensionVariantType instance_property_type(AttachedScriptInstance* instance,
                                              const godot::StringName* name,
                                              GDExtensionBool* valid) {
    const auto* property = find_property(instance, *name);
    *valid = property != nullptr;
    return property ? static_cast<GDExtensionVariantType>(property->info.type)
                    : GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool instance_has_method(AttachedScriptInstance* instance,
                                    const godot::StringName* name) {
    return find_method(instance, *name) != nullptr;
}

GDExtensionInt instance_method_argument_count(AttachedScriptInstance* instance,
                                              const godot::StringName* name,
                                              GDExtensionBool* valid) {
    const auto* method = find_method(instance, *name);
    *valid = method != nullptr;
    return method ? static_cast<GDExtensionInt>(method->arguments.size()) : 0;
}

void instance_call(AttachedScriptInstance* instance, const godot::StringName* method,
                   const godot::Variant** arguments, GDExtensionInt argument_count,
                   godot::Variant* result, GDExtensionCallError* error) {
    if (!find_method(instance, *method)) {
        error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return;
    }
    *result = call_behavior(instance, *method, arguments, argument_count, *error);
}

void instance_notification(AttachedScriptInstance* instance, std::int32_t what,
                           GDExtensionBool reversed) {
    instance->behavior->dispatch_notification(what, reversed);
}

void instance_to_string(AttachedScriptInstance* instance, GDExtensionBool* valid,
                        godot::String* result) {
    if (!find_method(instance, "_to_string")) {
        *valid = false;
        return;
    }
    GDExtensionCallError error{};
    const auto value = call_behavior(instance, "_to_string", nullptr, 0, error);
    *valid = error.error == GDEXTENSION_CALL_OK && value.get_type() == godot::Variant::STRING;
    if (*valid)
        *result = value;
}

void instance_refcount_incremented(AttachedScriptInstance*) {}

GDExtensionBool instance_refcount_decremented(AttachedScriptInstance*) { return true; }

GDExtensionObjectPtr instance_script(AttachedScriptInstance* instance) {
    return instance->script.ptr()->_owner;
}

GDExtensionBool instance_is_placeholder(AttachedScriptInstance*) { return false; }

GDExtensionScriptLanguagePtr instance_language(AttachedScriptInstance*) {
    const auto* language = AttachedCompiledLanguage::get_singleton();
    return language ? language->_owner : nullptr;
}

void instance_free(AttachedScriptInstance* instance) { godot::memdelete(instance); }

const GDExtensionScriptInstanceInfo3& script_instance_info() {
    static const GDExtensionScriptInstanceInfo3 value{
        reinterpret_cast<GDExtensionScriptInstanceSet>(instance_set),
        reinterpret_cast<GDExtensionScriptInstanceGet>(instance_get),
        reinterpret_cast<GDExtensionScriptInstanceGetPropertyList>(instance_property_list),
        reinterpret_cast<GDExtensionScriptInstanceFreePropertyList2>(instance_free_property_list),
        nullptr,
        reinterpret_cast<GDExtensionScriptInstancePropertyCanRevert>(instance_property_can_revert),
        reinterpret_cast<GDExtensionScriptInstancePropertyGetRevert>(instance_property_get_revert),
        reinterpret_cast<GDExtensionScriptInstanceGetOwner>(instance_owner),
        reinterpret_cast<GDExtensionScriptInstanceGetPropertyState>(instance_property_state),
        reinterpret_cast<GDExtensionScriptInstanceGetMethodList>(instance_method_list),
        reinterpret_cast<GDExtensionScriptInstanceFreeMethodList2>(instance_free_method_list),
        reinterpret_cast<GDExtensionScriptInstanceGetPropertyType>(instance_property_type),
        nullptr,
        reinterpret_cast<GDExtensionScriptInstanceHasMethod>(instance_has_method),
        reinterpret_cast<GDExtensionScriptInstanceGetMethodArgumentCount>(
            instance_method_argument_count),
        reinterpret_cast<GDExtensionScriptInstanceCall>(instance_call),
        reinterpret_cast<GDExtensionScriptInstanceNotification2>(instance_notification),
        reinterpret_cast<GDExtensionScriptInstanceToString>(instance_to_string),
        reinterpret_cast<GDExtensionScriptInstanceRefCountIncremented>(
            instance_refcount_incremented),
        reinterpret_cast<GDExtensionScriptInstanceRefCountDecremented>(
            instance_refcount_decremented),
        reinterpret_cast<GDExtensionScriptInstanceGetScript>(instance_script),
        reinterpret_cast<GDExtensionScriptInstanceIsPlaceholder>(instance_is_placeholder),
        nullptr,
        nullptr,
        reinterpret_cast<GDExtensionScriptInstanceGetLanguage>(instance_language),
        reinterpret_cast<GDExtensionScriptInstanceFree>(instance_free),
    };
    return value;
}

} // namespace

void* AttachedCompiledScript::_instance_create(godot::Object* object) const {
    const auto metadata = descriptor();
    if (!metadata || metadata->abstract || !metadata->factory || !object ||
        !object->is_class(metadata->native_base_type)) {
        return nullptr;
    }

    godot::Ref<AttachedScriptBehavior> behavior{metadata->factory()};
    if (behavior.is_null())
        return nullptr;
    behavior->attach_owner(object);

    godot::Ref<AttachedCompiledScript> script{const_cast<AttachedCompiledScript*>(this)};
    auto* instance = memnew(AttachedScriptInstance(object, std::move(script), *metadata, behavior));
    {
        std::lock_guard<std::mutex> lock{AttachedScriptInstance::instances_mutex()};
        if (!AttachedScriptInstance::instances().emplace(object, instance).second) {
            godot::memdelete(instance);
            return nullptr;
        }
    }
    behavior->initialize_instance();

    if (find_method(instance, "_init")) {
        const godot::Array empty_arguments;
        const auto* arguments = &empty_arguments;
        if (pending_construction && pending_construction->source_path == metadata->source_path) {
            arguments = pending_construction->arguments;
            pending_construction->consumed = true;
        }
        std::vector<const godot::Variant*> argument_pointers;
        argument_pointers.reserve(static_cast<std::size_t>(arguments->size()));
        for (std::int64_t index = 0; index < arguments->size(); ++index)
            argument_pointers.push_back(&(*arguments)[index]);
        GDExtensionCallError call_error{};
        call_behavior(instance, "_init", argument_pointers.data(), arguments->size(), call_error);
        if (call_error.error != GDEXTENSION_CALL_OK) {
            godot::memdelete(instance);
            return nullptr;
        }
    }
    return godot::gdextension_interface::script_instance_create3(&script_instance_info(), instance);
}

void* AttachedCompiledScript::_placeholder_instance_create(godot::Object*) const { return nullptr; }

bool AttachedCompiledScript::_instance_has(godot::Object* object) const {
    std::lock_guard<std::mutex> lock{AttachedScriptInstance::instances_mutex()};
    const auto found = AttachedScriptInstance::instances().find(object);
    return found != AttachedScriptInstance::instances().end() &&
           found->second->script.ptr() == this;
}

godot::Variant instantiate_attached_script(const godot::String& source_path,
                                           const godot::Array& arguments, godot::String* error) {
    const auto descriptor = resolve_attached_script(source_path, error);
    if (!descriptor)
        return {};
    auto* class_db = godot::ClassDBSingleton::get_singleton();
    if (!class_db || !class_db->can_instantiate(descriptor->native_base_type)) {
        if (error)
            *error = "attached native base is not instantiable: " +
                     godot::String{descriptor->native_base_type};
        return {};
    }

    godot::Variant instance = class_db->instantiate(descriptor->native_base_type);
    godot::Object* object = instance;
    if (!object) {
        if (error)
            *error = "ClassDB failed to instantiate attached native base: " +
                     godot::String{descriptor->native_base_type};
        return {};
    }

    godot::Ref<AttachedCompiledScript> script;
    script.instantiate();
    script->set_source_path(descriptor->source_path);
    PendingConstruction construction{descriptor->source_path, &arguments, false};
    auto* previous = pending_construction;
    pending_construction = &construction;
    object->set_script(script);
    pending_construction = previous;
    if (!script->_instance_has(object) ||
        (find_method(*descriptor, "_init") && !construction.consumed)) {
        if (error)
            *error = "failed to attach or initialize compiled script: " + descriptor->source_path;
        // ClassDB returns a strong Variant for RefCounted instances, but ordinary Objects remain
        // caller-owned. Release either ownership model before reporting construction failure.
        if (godot::Object::cast_to<godot::RefCounted>(object))
            instance = godot::Variant{};
        else
            godot::memdelete(object);
        return {};
    }
    return instance;
}

godot::Variant call_attached_native_base_raw(godot::Object* owner,
                                             const godot::StringName& native_class,
                                             const godot::StringName& method,
                                             const std::uint32_t compatibility_hash,
                                             const godot::Variant** arguments,
                                             const std::int64_t argument_count) {
    if (!owner || !owner->is_class(native_class) || argument_count < 0) {
        godot::UtilityFunctions::push_error(
            "GDPP: invalid owner or argument count for an attached native super call");
        return {};
    }
    GDExtensionMethodBindPtr method_bind{nullptr};
    {
        std::lock_guard<std::mutex> lock{native_bind_mutex()};
        const auto key = native_bind_key(native_class, method, compatibility_hash);
        const auto found = native_bind_cache().find(key);
        if (found != native_bind_cache().end()) {
            method_bind = found->second;
        } else {
            method_bind = godot::gdextension_interface::classdb_get_method_bind(
                native_class._native_ptr(), method._native_ptr(), compatibility_hash);
            if (method_bind)
                native_bind_cache().emplace(key, method_bind);
        }
    }
    if (!method_bind) {
        godot::UtilityFunctions::push_error(
            godot::String{"GDPP: cannot resolve attached native super method "} +
            godot::String{native_class} + "." + godot::String{method});
        return {};
    }

    godot::Variant result;
    GDExtensionCallError error{};
    godot::gdextension_interface::object_method_bind_call(
        method_bind, owner->_owner, reinterpret_cast<GDExtensionConstVariantPtr*>(arguments),
        argument_count, result._native_ptr(), &error);
    if (error.error != GDEXTENSION_CALL_OK) {
        godot::UtilityFunctions::push_error(
            godot::String{"GDPP: attached native super call failed for "} +
            godot::String{native_class} + "." + godot::String{method});
        return {};
    }
    return result;
}

} // namespace gdpp::runtime
