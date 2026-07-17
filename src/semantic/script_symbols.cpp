#include "gdpp/semantic/script_symbols.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gdpp {

void ScriptSymbolTable::add(ScriptClassSymbol symbol) {
    const auto index = classes_.size();
    paths_.emplace(symbol.path, index);
    native_classes_.emplace(symbol.native_class_name, index);
    if (ambiguous_class_names_.find(symbol.script_name) == ambiguous_class_names_.end()) {
        const auto [existing, unique] = class_names_.emplace(symbol.script_name, index);
        if (!unique) {
            class_names_.erase(existing);
            ambiguous_class_names_.insert(symbol.script_name);
        }
    }
    if (symbol.globally_named)
        globals_.emplace(symbol.script_name, index);
    if (!symbol.autoload_name.empty()) {
        autoloads_.emplace(symbol.autoload_name, index);
        globals_.emplace(symbol.script_name, index);
    }
    classes_.push_back(std::move(symbol));
}

void ScriptSymbolTable::add_external(ExternalClassSymbol symbol) {
    if (external_names_.find(symbol.name) != external_names_.end())
        return;
    const auto index = external_classes_.size();
    external_names_.emplace(symbol.name, index);
    external_classes_.push_back(std::move(symbol));
}

void ScriptSymbolTable::add_resource_alias(std::string reference, std::string project_path) {
    resource_aliases_.insert_or_assign(std::move(reference), std::move(project_path));
}

const ScriptClassSymbol* ScriptSymbolTable::find_path(const std::string& path) const noexcept {
    const auto found = paths_.find(path);
    return found == paths_.end() ? nullptr : &classes_[found->second];
}

const ScriptClassSymbol*
ScriptSymbolTable::resolve_path(const std::string& owner_path,
                                const std::string& reference) const noexcept {
    const auto resource_path = resolve_resource_path(owner_path, reference);
    return resource_path ? find_path(*resource_path) : nullptr;
}

std::optional<std::string>
ScriptSymbolTable::resolve_resource_path(const std::string& owner_path,
                                         const std::string& reference) const noexcept {
    if (reference.rfind("uid://", 0) == 0) {
        const auto found = resource_aliases_.find(reference);
        return found == resource_aliases_.end() ? std::nullopt
                                                : std::optional<std::string>{found->second};
    }
    constexpr std::string_view resource_prefix{"res://"};
    std::filesystem::path path;
    if (reference.rfind(resource_prefix, 0) == 0)
        path = reference.substr(resource_prefix.size());
    else
        path = std::filesystem::path{owner_path}.parent_path() / reference;
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || *normalized.begin() == "..")
        return std::nullopt;
    return normalized.generic_string();
}

const ScriptClassSymbol* ScriptSymbolTable::find_global(const std::string& name) const noexcept {
    const auto found = globals_.find(name);
    return found == globals_.end() ? nullptr : &classes_[found->second];
}

const ScriptClassSymbol* ScriptSymbolTable::find_class(const std::string& name) const noexcept {
    if (const auto global = globals_.find(name); global != globals_.end())
        return &classes_[global->second];
    const auto found = class_names_.find(name);
    return found == class_names_.end() ? nullptr : &classes_[found->second];
}

const ScriptClassSymbol*
ScriptSymbolTable::find_native_class(const std::string& name) const noexcept {
    const auto found = native_classes_.find(name);
    return found == native_classes_.end() ? nullptr : &classes_[found->second];
}

const ScriptClassSymbol* ScriptSymbolTable::find_autoload(const std::string& name) const noexcept {
    const auto found = autoloads_.find(name);
    return found == autoloads_.end() ? nullptr : &classes_[found->second];
}

const ExternalClassSymbol*
ScriptSymbolTable::find_external(const std::string& name) const noexcept {
    const auto found = external_names_.find(name);
    return found == external_names_.end() ? nullptr : &external_classes_[found->second];
}

const ScriptMemberSymbol*
ScriptSymbolTable::find_external_member(const ExternalClassSymbol& owner,
                                        const std::string& name) const noexcept {
    const auto found =
        std::find_if(owner.members.begin(), owner.members.end(),
                     [&](const ScriptMemberSymbol& member) { return member.name == name; });
    return found == owner.members.end() ? nullptr : &*found;
}

const ScriptEnumSymbol*
ScriptSymbolTable::find_external_enum(const ExternalClassSymbol& owner,
                                      const std::string& name) const noexcept {
    const auto found =
        std::find_if(owner.enums.begin(), owner.enums.end(),
                     [&](const auto& enumeration) { return enumeration.name == name; });
    return found == owner.enums.end() ? nullptr : &*found;
}

const ScriptClassSymbol* ScriptSymbolTable::base_of(const ScriptClassSymbol& owner) const noexcept {
    return owner.base_script_path.empty() ? nullptr : find_path(owner.base_script_path);
}

const ScriptMemberSymbol* ScriptSymbolTable::find_member(const ScriptClassSymbol& owner,
                                                         const std::string& name) const noexcept {
    const ScriptClassSymbol* current = &owner;
    for (std::size_t depth = 0; current && depth <= classes_.size(); ++depth) {
        for (const auto& member : current->members) {
            if (member.name == name)
                return &member;
        }
        current =
            current->base_script_path.empty() ? nullptr : find_path(current->base_script_path);
    }
    return nullptr;
}

const ScriptEnumSymbol* ScriptSymbolTable::find_enum(const ScriptClassSymbol& owner,
                                                     const std::string& name) const noexcept {
    const ScriptClassSymbol* current = &owner;
    for (std::size_t depth = 0; current && depth <= classes_.size(); ++depth) {
        for (const auto& enumeration : current->enums) {
            if (enumeration.name == name)
                return &enumeration;
        }
        current =
            current->base_script_path.empty() ? nullptr : find_path(current->base_script_path);
    }
    return nullptr;
}

const ScriptInnerClassSymbol*
ScriptSymbolTable::find_inner(const ScriptClassSymbol& owner,
                              const std::string& name) const noexcept {
    const auto separator = name.rfind('.');
    const auto local_name = separator == std::string::npos ? name : name.substr(separator + 1);
    const auto found = std::find_if(owner.inner_classes.begin(), owner.inner_classes.end(),
                                    [&](const auto& inner) { return inner.name == local_name; });
    return found == owner.inner_classes.end() ? nullptr : &*found;
}

const ScriptMemberSymbol*
ScriptSymbolTable::find_inner_member(const ScriptInnerClassSymbol& owner,
                                     const std::string& name) const noexcept {
    const auto found = std::find_if(owner.members.begin(), owner.members.end(),
                                    [&](const auto& member) { return member.name == name; });
    return found == owner.members.end() ? nullptr : &*found;
}

std::vector<const ScriptMemberSymbol*>
ScriptSymbolTable::inherited_members(const ScriptClassSymbol& owner) const {
    std::vector<const ScriptMemberSymbol*> result;
    std::unordered_set<std::string> names;
    const auto* current =
        owner.base_script_path.empty() ? nullptr : find_path(owner.base_script_path);
    for (std::size_t depth = 0; current && depth <= classes_.size(); ++depth) {
        for (const auto& member : current->members) {
            if (names.insert(member.name).second)
                result.push_back(&member);
        }
        current =
            current->base_script_path.empty() ? nullptr : find_path(current->base_script_path);
    }
    return result;
}

bool ScriptSymbolTable::inherits(const std::string& derived_global_name,
                                 const std::string& base_global_name) const noexcept {
    if (derived_global_name == base_global_name)
        return true;
    const auto* current = find_global(derived_global_name);
    for (std::size_t depth = 0; current && depth <= classes_.size(); ++depth) {
        current =
            current->base_script_path.empty() ? nullptr : find_path(current->base_script_path);
        if (current && current->globally_named && current->script_name == base_global_name)
            return true;
    }
    return false;
}

bool ScriptSymbolTable::inherits(const ScriptClassSymbol& derived,
                                 const std::string& base_global_name) const noexcept {
    const ScriptClassSymbol* current = &derived;
    for (std::size_t depth = 0; current && depth <= classes_.size(); ++depth) {
        if (current->globally_named && current->script_name == base_global_name)
            return true;
        current = base_of(*current);
    }
    return false;
}

bool ScriptSymbolTable::requires_dynamic_dispatch(const ScriptClassSymbol& owner,
                                                  const std::string& method) const noexcept {
    const auto* contract = find_member(owner, method);
    if (!contract || contract->kind != ScriptMemberKind::function || contract->is_static)
        return false;
    const auto same_native_abi = [](const ScriptMemberSymbol& left,
                                    const ScriptMemberSymbol& right) {
        return left.type == right.type && left.parameters == right.parameters &&
               left.default_parameters == right.default_parameters;
    };
    for (const auto& candidate : classes_) {
        if (candidate.path == owner.path)
            continue;
        bool descendant = false;
        const ScriptClassSymbol* base = base_of(candidate);
        for (std::size_t depth = 0; base && depth <= classes_.size(); ++depth) {
            if (base->path == owner.path) {
                descendant = true;
                break;
            }
            base = base_of(*base);
        }
        if (!descendant)
            continue;
        const auto override = std::find_if(candidate.members.begin(), candidate.members.end(),
                                           [&](const auto& member) {
                                               return member.kind == ScriptMemberKind::function &&
                                                      !member.is_static && member.name == method;
                                           });
        if (override != candidate.members.end() && !same_native_abi(*contract, *override))
            return true;
    }
    return false;
}

} // namespace gdpp
