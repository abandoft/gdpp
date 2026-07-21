#include "gdpp/runtime/attached_script.hpp"

#include <godot_cpp/core/error_macros.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace gdpp::runtime {
namespace {

std::mutex& registry_mutex() {
    static std::mutex value;
    return value;
}

std::map<std::string, AttachedScriptDescriptor>& registry() {
    static std::map<std::string, AttachedScriptDescriptor> value;
    return value;
}

std::string registry_key(const godot::String& path) {
    const godot::CharString utf8 = path.utf8();
    return {utf8.get_data(), static_cast<std::size_t>(utf8.length())};
}

void set_error(godot::String* destination, const godot::String& message) {
    if (destination)
        *destination = message;
}

bool same_identity(const AttachedScriptDescriptor& left, const AttachedScriptDescriptor& right) {
    return left.source_path == right.source_path && left.global_name == right.global_name &&
           left.native_base_type == right.native_base_type &&
           left.base_script_path == right.base_script_path &&
           left.behavior_class == right.behavior_class && left.factory == right.factory &&
           left.tool == right.tool && left.abstract == right.abstract &&
           left.properties.size() == right.properties.size() &&
           left.methods.size() == right.methods.size() &&
           left.signals.size() == right.signals.size();
}

template <typename Items, typename Name>
bool names_are_unique(const Items& items, Name&& name, godot::String* duplicate) {
    std::unordered_set<std::string> names;
    for (const auto& item : items) {
        const auto key = registry_key(godot::String{name(item)});
        if (!names.emplace(key).second) {
            if (duplicate)
                *duplicate = godot::String{name(item)};
            return false;
        }
    }
    return true;
}

} // namespace

void AttachedScriptBehavior::attach_owner(godot::Object* owner) {
    ERR_FAIL_NULL(owner);
    ERR_FAIL_COND_MSG(owner_ != nullptr && owner_ != owner,
                      "A GDPP attached behavior cannot be moved between owners");
    owner_ = owner;
}

void AttachedScriptBehavior::detach_owner() { owner_ = nullptr; }

godot::Object* AttachedScriptBehavior::owner() const { return owner_; }

void AttachedScriptBehavior::initialize_instance() {}

void AttachedScriptBehavior::dispatch_notification(std::int32_t, bool) {}

void AttachedScriptBehavior::_bind_methods() {}

bool register_attached_script(AttachedScriptDescriptor descriptor, godot::String* error) {
    descriptor.source_path = descriptor.source_path.simplify_path();
    if (descriptor.source_path.is_empty() || !descriptor.source_path.begins_with("res://")) {
        set_error(error, "attached script source_path must be an absolute res:// path");
        return false;
    }
    if (descriptor.native_base_type.is_empty()) {
        set_error(error, "attached script native_base_type must not be empty");
        return false;
    }
    if (descriptor.behavior_class.is_empty()) {
        set_error(error, "attached script behavior_class must not be empty");
        return false;
    }
    if (!descriptor.abstract && descriptor.factory == nullptr) {
        set_error(error, "concrete attached script must provide a behavior factory");
        return false;
    }
    if (!descriptor.base_script_path.is_empty() &&
        !descriptor.base_script_path.begins_with("res://")) {
        set_error(error, "attached base_script_path must be empty or an absolute res:// path");
        return false;
    }

    godot::String duplicate;
    if (!names_are_unique(
            descriptor.properties,
            [](const AttachedScriptProperty& item) { return item.info.name; }, &duplicate)) {
        set_error(error, "duplicate attached script property: " + duplicate);
        return false;
    }
    if (!names_are_unique(
            descriptor.methods, [](const godot::MethodInfo& item) { return item.name; },
            &duplicate)) {
        set_error(error, "duplicate attached script method: " + duplicate);
        return false;
    }
    if (!names_are_unique(
            descriptor.signals, [](const godot::MethodInfo& item) { return item.name; },
            &duplicate)) {
        set_error(error, "duplicate attached script signal: " + duplicate);
        return false;
    }

    std::lock_guard<std::mutex> lock{registry_mutex()};
    const auto key = registry_key(descriptor.source_path);
    const auto existing = registry().find(key);
    if (existing != registry().end()) {
        if (same_identity(existing->second, descriptor))
            return true;
        set_error(error, "conflicting attached script descriptor: " + descriptor.source_path);
        return false;
    }
    registry().emplace(key, std::move(descriptor));
    return true;
}

void unregister_all_attached_scripts() {
    std::lock_guard<std::mutex> lock{registry_mutex()};
    registry().clear();
}

std::optional<AttachedScriptDescriptor> find_attached_script(const godot::String& source_path) {
    std::lock_guard<std::mutex> lock{registry_mutex()};
    const auto found = registry().find(registry_key(source_path.simplify_path()));
    if (found == registry().end())
        return std::nullopt;
    return found->second;
}

std::optional<AttachedScriptDescriptor> resolve_attached_script(const godot::String& source_path,
                                                                godot::String* error) {
    std::lock_guard<std::mutex> lock{registry_mutex()};
    const auto root = registry().find(registry_key(source_path.simplify_path()));
    if (root == registry().end()) {
        set_error(error, "attached script descriptor is not registered: " + source_path);
        return std::nullopt;
    }

    AttachedScriptDescriptor resolved = root->second;
    std::unordered_set<std::string> visited;
    visited.emplace(root->first);
    auto base_path = resolved.base_script_path;
    const auto append_unique = [](auto& destination, const auto& inherited, auto&& name) {
        for (const auto& item : inherited) {
            const auto duplicate =
                std::any_of(destination.begin(), destination.end(),
                            [&](const auto& existing) { return name(existing) == name(item); });
            if (!duplicate)
                destination.push_back(item);
        }
    };

    while (!base_path.is_empty()) {
        const auto key = registry_key(base_path.simplify_path());
        if (!visited.emplace(key).second) {
            set_error(error, "cyclic attached script inheritance at: " + base_path);
            return std::nullopt;
        }
        const auto base = registry().find(key);
        if (base == registry().end()) {
            set_error(error, "attached base script descriptor is not registered: " + base_path);
            return std::nullopt;
        }
        if (base->second.native_base_type != resolved.native_base_type) {
            set_error(error,
                      "attached script inheritance changes native base type at: " + base_path);
            return std::nullopt;
        }
        if (base->second.rpc_config.get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary rpc = resolved.rpc_config.get_type() == godot::Variant::DICTIONARY
                                        ? godot::Dictionary{resolved.rpc_config}
                                        : godot::Dictionary{};
            const godot::Dictionary inherited_rpc = base->second.rpc_config;
            const godot::Array method_names = inherited_rpc.keys();
            for (std::int64_t index = 0; index < method_names.size(); ++index) {
                const godot::StringName method_name{method_names[index]};
                const bool overridden = std::any_of(
                    resolved.methods.begin(), resolved.methods.end(),
                    [&](const godot::MethodInfo& method) { return method.name == method_name; });
                if (!overridden && !rpc.has(method_name))
                    rpc[method_name] = inherited_rpc[method_name];
            }
            if (!rpc.is_empty())
                resolved.rpc_config = rpc;
        }
        append_unique(resolved.properties, base->second.properties,
                      [](const AttachedScriptProperty& item) { return item.info.name; });
        append_unique(resolved.methods, base->second.methods,
                      [](const godot::MethodInfo& item) { return item.name; });
        append_unique(resolved.signals, base->second.signals,
                      [](const godot::MethodInfo& item) { return item.name; });
        const godot::Array constant_names = base->second.constants.keys();
        for (std::int64_t index = 0; index < constant_names.size(); ++index) {
            const godot::Variant& name = constant_names[index];
            if (!resolved.constants.has(name))
                resolved.constants[name] = base->second.constants[name];
        }
        base_path = base->second.base_script_path;
    }
    return resolved;
}

std::vector<godot::String> attached_script_paths() {
    std::lock_guard<std::mutex> lock{registry_mutex()};
    std::vector<godot::String> result;
    result.reserve(registry().size());
    for (const auto& [path, descriptor] : registry())
        result.push_back(descriptor.source_path);
    return result;
}

} // namespace gdpp::runtime
