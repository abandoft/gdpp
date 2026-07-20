#pragma once

#include "gdpp/semantic/type.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gdpp {

enum class ScriptMemberKind { field, constant, function, signal, enum_value };

struct ScriptMemberSymbol {
    ScriptMemberKind kind{ScriptMemberKind::field};
    std::string name;
    Type type;
    std::vector<Type> parameters;
    std::size_t required_arguments{0};
    bool is_static{false};
    bool has_accessor{false};
    // Source-level annotation metadata is kept separate from the resolved native ABI.
    // Project inheritance can therefore inherit omitted types without losing the fact that
    // the author did (or did not) explicitly constrain an override.
    bool has_explicit_type{false};
    std::vector<bool> explicit_parameter_types;
    std::vector<bool> default_parameters;
    bool is_vararg{false};
    bool read_only{false};
    std::int64_t constant_value{0};
    bool is_coroutine{false};
    bool is_abstract{false};
};

struct ScriptEnumEntrySymbol {
    std::string name;
    std::int64_t value{0};
};

struct ScriptEnumSymbol {
    std::string name;
    std::vector<ScriptEnumEntrySymbol> entries;
    bool is_bitfield{false};
};

struct ScriptInnerClassSymbol {
    std::string name;
    std::string native_class_name;
    std::string godot_base_type{"RefCounted"};
    std::string base_class_name;
    std::vector<ScriptMemberSymbol> members;
    std::vector<ScriptEnumSymbol> enums;
    bool is_abstract{false};
};

struct ScriptClassSymbol {
    std::string path;
    std::string script_name;
    std::string native_class_name;
    std::string header_file_name;
    std::string godot_base_type{"Node"};
    std::string base_script_path;
    std::string autoload_name;
    bool globally_named{false};
    bool is_abstract{false};
    std::vector<ScriptMemberSymbol> members;
    std::vector<ScriptEnumSymbol> enums;
    std::vector<ScriptInnerClassSymbol> inner_classes;
};

struct ExternalClassSymbol {
    std::string name;
    std::string godot_base_type{"Object"};
    std::string provider_abi;
    bool runtime_only{true};
    bool members_complete{false};
    std::vector<ScriptMemberSymbol> members;
    std::vector<ScriptEnumSymbol> enums;
};

class ScriptSymbolTable final {
  public:
    void add(ScriptClassSymbol symbol);
    void add_external(ExternalClassSymbol symbol);
    void add_resource_alias(std::string reference, std::string project_path);
    bool set_coroutine(const std::string& path, const std::string& inner_class,
                       const std::string& method, bool coroutine);
    void update_class_identity(const std::string& path, std::string native_class_name,
                               std::string header_file_name);

    [[nodiscard]] const ScriptClassSymbol* find_path(const std::string& path) const noexcept;
    [[nodiscard]] const ScriptClassSymbol*
    resolve_path(const std::string& owner_path, const std::string& reference) const noexcept;
    [[nodiscard]] std::optional<std::string>
    resolve_resource_path(const std::string& owner_path,
                          const std::string& reference) const noexcept;
    [[nodiscard]] const ScriptClassSymbol* find_global(const std::string& name) const noexcept;
    // Global class_name declarations are authoritative source-language identities. If an
    // unnamed path script happens to have the same display stem, the global declaration wins;
    // otherwise an unnamed script is returned only when its display name is unique. This is
    // also used for types produced from already resolved script resources.
    [[nodiscard]] const ScriptClassSymbol* find_class(const std::string& name) const noexcept;
    [[nodiscard]] const ScriptClassSymbol*
    find_native_class(const std::string& name) const noexcept;
    [[nodiscard]] const ScriptClassSymbol* find_autoload(const std::string& name) const noexcept;
    [[nodiscard]] const ExternalClassSymbol* find_external(const std::string& name) const noexcept;
    [[nodiscard]] const ScriptMemberSymbol*
    find_external_member(const ExternalClassSymbol& owner, const std::string& name) const noexcept;
    [[nodiscard]] const ScriptEnumSymbol*
    find_external_enum(const ExternalClassSymbol& owner, const std::string& name) const noexcept;
    [[nodiscard]] const ScriptClassSymbol* base_of(const ScriptClassSymbol& owner) const noexcept;
    [[nodiscard]] const ScriptMemberSymbol* find_member(const ScriptClassSymbol& owner,
                                                        const std::string& name) const noexcept;
    [[nodiscard]] const ScriptEnumSymbol* find_enum(const ScriptClassSymbol& owner,
                                                    const std::string& name) const noexcept;
    [[nodiscard]] const ScriptInnerClassSymbol* find_inner(const ScriptClassSymbol& owner,
                                                           const std::string& name) const noexcept;
    [[nodiscard]] const ScriptMemberSymbol*
    find_inner_member(const ScriptInnerClassSymbol& owner, const std::string& name) const noexcept;
    [[nodiscard]] std::vector<const ScriptMemberSymbol*>
    inherited_members(const ScriptClassSymbol& owner) const;
    [[nodiscard]] bool inherits(const std::string& derived_global_name,
                                const std::string& base_global_name) const noexcept;
    [[nodiscard]] bool inherits(const ScriptClassSymbol& derived,
                                const std::string& base_global_name) const noexcept;
    // GDScript permits derived methods to change annotations and optional arguments. Such a
    // hierarchy cannot use a direct C++ virtual call safely and must enter through Godot's
    // dynamic method dispatcher when the receiver is typed as the base script.
    [[nodiscard]] bool requires_dynamic_dispatch(const ScriptClassSymbol& owner,
                                                 const std::string& method) const noexcept;
    [[nodiscard]] bool may_dispatch_coroutine(const ScriptClassSymbol& owner,
                                              const std::string& method) const noexcept;

  private:
    std::vector<ScriptClassSymbol> classes_;
    std::unordered_map<std::string, std::size_t> paths_;
    std::unordered_map<std::string, std::size_t> globals_;
    std::unordered_map<std::string, std::size_t> class_names_;
    std::unordered_map<std::string, std::size_t> native_classes_;
    std::unordered_set<std::string> ambiguous_class_names_;
    std::unordered_map<std::string, std::size_t> autoloads_;
    std::unordered_map<std::string, std::string> resource_aliases_;
    std::vector<ExternalClassSymbol> external_classes_;
    std::unordered_map<std::string, std::size_t> external_names_;
};

} // namespace gdpp
