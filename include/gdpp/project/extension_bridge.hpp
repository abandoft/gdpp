#pragma once

#include "gdpp/core/godot_version.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gdpp {

enum class ExtensionBridgeMemberKind { property, method, signal, constant };

struct ExtensionBridgeParameter {
    std::string name;
    std::string type{"Variant"};
    bool has_default{false};
};

struct ExtensionBridgeMember {
    ExtensionBridgeMemberKind kind{ExtensionBridgeMemberKind::property};
    std::string name;
    std::string type{"Variant"};
    std::vector<ExtensionBridgeParameter> parameters;
    bool read_only{false};
    bool vararg{false};
    bool is_static{false};
    std::int64_t constant_value{0};
};

struct ExtensionBridgeEnumEntry {
    std::string name;
    std::int64_t value{0};
};

struct ExtensionBridgeEnum {
    std::string name;
    bool is_bitfield{false};
    std::vector<ExtensionBridgeEnumEntry> entries;
};

struct ExtensionBridgeClass {
    std::string gdscript_name;
    std::string cpp_type;
    std::string header;
    std::string godot_base;
    // Runtime bridges describe classes registered in ClassDB by a binary-only provider.
    // They deliberately use Variant dispatch and therefore require neither provider headers
    // nor a native link dependency. Cross-library native GDExtension inheritance is rejected.
    bool runtime_only{false};
    // If true, an undeclared member is a compile-time error. Providers can leave this false while
    // migrating a legacy binary-only API, then enable fail-closed typo checking once complete.
    bool members_complete{false};
    std::vector<ExtensionBridgeMember> members;
    std::vector<ExtensionBridgeEnum> enums;
};

struct ExtensionBridgeTarget {
    std::string platform;
    std::string architecture;
    std::string profile;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> link_libraries;
};

struct ExtensionBridge {
    std::filesystem::path manifest_path;
    std::filesystem::path provider_descriptor;
    std::string provider;
    std::string abi;
    // Exact manifest bytes participate in compiler cache identity even if a provider forgets to
    // bump its declared ABI token. The token is still mandatory for binary changes outside JSON.
    std::string contract_hash;
    GodotVersion minimum_godot_version{gdpp::minimum_godot_version};
    std::vector<ExtensionBridgeClass> classes;
    std::vector<ExtensionBridgeTarget> targets;
};

struct ExtensionBridgeLoadResult {
    std::vector<ExtensionBridge> bridges;
    std::vector<std::string> diagnostics;
};

[[nodiscard]] ExtensionBridgeLoadResult
load_extension_bridges(const std::filesystem::path& project_root,
                       const std::vector<std::filesystem::path>& manifests,
                       GodotVersion target_version);

[[nodiscard]] std::optional<ExtensionBridgeTarget>
select_extension_bridge_target(const ExtensionBridge& bridge, std::string_view platform,
                               std::string_view architecture, std::string_view profile);

[[nodiscard]] std::string write_extension_bridge_lock(const std::vector<ExtensionBridge>& bridges);

} // namespace gdpp
