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

bool same_identity(const AttachedScriptDescriptor& left,
                   const AttachedScriptDescriptor& right) {
    return left.source_path == right.source_path && left.global_name == right.global_name &&
           left.native_base_type == right.native_base_type &&
           left.base_script_path == right.base_script_path &&
           left.behavior_class == right.behavior_class && left.factory == right.factory &&
           left.tool == right.tool && left.abstract == right.abstract &&
           left.properties.size() == right.properties.size() &&
           left.methods.size() == right.methods.size() && left.signals.size() == right.signals.size();
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
    if (!names_are_unique(descriptor.properties,
                          [](const AttachedScriptProperty& item) { return item.info.name; },
                          &duplicate)) {
        set_error(error, "duplicate attached script property: " + duplicate);
        return false;
    }
    if (!names_are_unique(descriptor.methods,
                          [](const godot::MethodInfo& item) { return item.name; }, &duplicate)) {
        set_error(error, "duplicate attached script method: " + duplicate);
        return false;
    }
    if (!names_are_unique(descriptor.signals,
                          [](const godot::MethodInfo& item) { return item.name; }, &duplicate)) {
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

std::vector<godot::String> attached_script_paths() {
    std::lock_guard<std::mutex> lock{registry_mutex()};
    std::vector<godot::String> result;
    result.reserve(registry().size());
    for (const auto& [path, descriptor] : registry())
        result.push_back(descriptor.source_path);
    return result;
}

} // namespace gdpp::runtime
