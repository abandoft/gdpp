#include "compiler_service.hpp"

#include "gdpp/compiler/compiler.hpp"
#include "gdpp/core/diagnostic.hpp"
#include "gdpp/core/source.hpp"
#include "gdpp/project/native_builder.hpp"
#include "gdpp/project/project_compiler.hpp"
#include "gdpp/semantic/godot_api.hpp"
#include "gdpp/support/path_utf8.hpp"
#include "gdpp/support/sha256.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#ifndef GDPP_SDK_ROOT
#define GDPP_SDK_ROOT ""
#endif
#ifndef GDPP_PLATFORM
#define GDPP_PLATFORM "linux"
#endif
#ifndef GDPP_ARCH
#define GDPP_ARCH "x86_64"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace gdpp::extension {
namespace {

std::string native_string(const godot::String& value) {
    const auto utf8 = value.utf8();
    return {utf8.get_data(), static_cast<std::size_t>(utf8.length())};
}

godot::Dictionary result_dictionary(const CompileResult& result, const SourceFile& source) {
    godot::Dictionary output;
    output["success"] = result.success;
    godot::PackedStringArray diagnostics;
    for (const auto& diagnostic : result.diagnostics) {
        diagnostics.push_back(godot::String{format_diagnostic(diagnostic, source, false).c_str()});
    }
    output["diagnostics"] = diagnostics;
    godot::Dictionary optimization;
    optimization["constants_folded"] = static_cast<int64_t>(result.optimization.constants_folded);
    optimization["statements_removed"] =
        static_cast<int64_t>(result.optimization.statements_removed);
    optimization["hir_branches_simplified"] =
        static_cast<int64_t>(result.optimization.branches_simplified);
    optimization["mir_branches_simplified"] =
        static_cast<int64_t>(result.mir_optimization.branches_simplified);
    optimization["mir_blocks_removed"] =
        static_cast<int64_t>(result.mir_optimization.blocks_removed);
    optimization["mir_instructions_removed"] =
        static_cast<int64_t>(result.mir_optimization.instructions_removed);
    output["optimization"] = optimization;
    if (result.success) {
        output["class_name"] = godot::String{result.unit.script_class_name.c_str()};
        output["native_class_name"] = godot::String{result.unit.class_name.c_str()};
        output["header_name"] = godot::String{result.unit.header_file_name.c_str()};
        output["source_name"] = godot::String{result.unit.source_file_name.c_str()};
        output["header"] = godot::String{result.unit.header.c_str()};
        output["source"] = godot::String{result.unit.source.c_str()};
    }
    return output;
}

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream stream{path, std::ios::binary};
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

bool write_file_atomic(const std::filesystem::path& path, const std::string& content) {
    auto temporary = path;
    temporary += ".tmp";
    if (!write_file(temporary, content)) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (!error)
        return true;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (!error)
        return true;
    std::filesystem::remove(temporary, error);
    return false;
}

bool has_path_component(const std::filesystem::path& path, std::string_view component) {
    return std::any_of(path.begin(), path.end(),
                       [&](const auto& item) { return item == std::filesystem::path{component}; });
}

bool is_xcframework(const std::filesystem::path& path) {
    std::error_code error;
    return path.extension() == ".xcframework" && !std::filesystem::is_symlink(path, error) &&
           !error && std::filesystem::is_directory(path, error) && !error &&
           std::filesystem::is_regular_file(path / "Info.plist", error) && !error;
}

bool commit_xcframework(const std::filesystem::path& pending,
                        const std::filesystem::path& destination, std::string& diagnostic) {
    const auto filename = destination.filename().string();
    if (!is_xcframework(pending) || pending.filename() != destination.filename() ||
        filename.rfind("libgdpp_project.", 0) != 0 ||
        destination.parent_path().filename() != "binary" ||
        !has_path_component(pending, "native-direct") ||
        !has_path_component(pending, "xcframework-staging")) {
        diagnostic = "refusing to commit an invalid or unsafe iOS XCFramework artifact";
        return false;
    }

    auto backup = destination;
    backup += ".previous";
    std::error_code error;
    bool had_destination = std::filesystem::exists(destination, error);
    if (error) {
        diagnostic = "cannot inspect the current iOS artifact: " + error.message();
        return false;
    }
    const bool has_backup = std::filesystem::exists(backup, error);
    if (error) {
        diagnostic = "cannot inspect the previous iOS artifact backup: " + error.message();
        return false;
    }
    if (has_backup && !had_destination) {
        std::filesystem::rename(backup, destination, error);
        if (error) {
            diagnostic = "cannot recover the previous iOS artifact after an interrupted commit: " +
                         error.message();
            return false;
        }
        had_destination = true;
    } else if (has_backup) {
        std::filesystem::remove_all(backup, error);
        if (error) {
            diagnostic = "cannot clear the obsolete iOS artifact backup: " + error.message();
            return false;
        }
    }
    if (had_destination) {
        std::filesystem::rename(destination, backup, error);
        if (error) {
            diagnostic =
                "cannot stage the current iOS artifact for replacement: " + error.message();
            return false;
        }
    }
    std::filesystem::rename(pending, destination, error);
    if (error) {
        const auto commit_error = error.message();
        if (had_destination) {
            error.clear();
            std::filesystem::rename(backup, destination, error);
        }
        diagnostic = "cannot commit the new iOS XCFramework: " + commit_error;
        return false;
    }
    std::filesystem::remove_all(backup, error);
    // A failed cleanup does not invalidate the newly committed XCFramework. The next build will
    // recognize and remove this backup before attempting another replacement.
    return true;
}

NativePlatform native_platform() {
    const std::string platform{GDPP_PLATFORM};
    if (platform == "macos")
        return NativePlatform::macos;
    if (platform == "windows")
        return NativePlatform::windows;
    return NativePlatform::linux;
}

std::string native_platform_name(NativePlatform platform) {
    if (platform == NativePlatform::macos)
        return "macos";
    if (platform == NativePlatform::windows)
        return "windows";
    if (platform == NativePlatform::android)
        return "android";
    if (platform == NativePlatform::ios)
        return "ios";
    if (platform == NativePlatform::web)
        return "web";
    return "linux";
}

std::string host_process_architecture() {
    if (const auto* engine = godot::Engine::get_singleton()) {
        auto architecture = native_string(engine->get_architecture_name());
        std::transform(architecture.begin(), architecture.end(), architecture.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (architecture == "aarch64" || architecture == "arm64" || architecture == "arm64-v8a") {
            return "arm64";
        }
        if (architecture == "amd64" || architecture == "x64" || architecture == "x86_64") {
            return "x86_64";
        }
    }

    // A universal Mach-O contains separate compiler-service slices. These preprocessor checks
    // identify the slice selected by the current process, whereas GDPP_ARCH intentionally names
    // the distributable file as "universal".
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return GDPP_ARCH;
#endif
}

std::optional<NativePlatform> parse_native_platform(const std::string& value) {
    if (value == "macos")
        return NativePlatform::macos;
    if (value == "windows")
        return NativePlatform::windows;
    if (value == "linux")
        return NativePlatform::linux;
    if (value == "android")
        return NativePlatform::android;
    if (value == "ios")
        return NativePlatform::ios;
    if (value == "web")
        return NativePlatform::web;
    return std::nullopt;
}

godot::String default_compiler() {
    return native_platform() == NativePlatform::windows ? godot::String{"cl.exe"}
                                                        : godot::String{"c++"};
}

std::string reflected_type_name(const godot::Dictionary& info, const bool allow_void) {
    if (!info.has("type"))
        return allow_void ? "void" : "Variant";
    const auto raw_type = static_cast<std::int64_t>(info["type"]);
    if (raw_type < godot::Variant::NIL || raw_type >= godot::Variant::VARIANT_MAX)
        return "Variant";
    const auto type = static_cast<godot::Variant::Type>(raw_type);
    const auto usage =
        info.has("usage")
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(info["usage"]))
            : std::uint64_t{0};
    if (type == godot::Variant::NIL) {
        return (usage & godot::PROPERTY_USAGE_NIL_IS_VARIANT) != 0 || !allow_void ? "Variant"
                                                                                  : "void";
    }
    if ((type == godot::Variant::OBJECT || type == godot::Variant::INT) && info.has("class_name")) {
        const godot::StringName class_name = info["class_name"];
        if (!class_name.is_empty())
            return native_string(godot::String{class_name});
    }
    const auto hint =
        info.has("hint") ? static_cast<std::int64_t>(info["hint"]) : std::int64_t{-1};
    const auto hint_string = info.has("hint_string")
                                 ? native_string(static_cast<godot::String>(info["hint_string"]))
                                 : std::string{};
    if (type == godot::Variant::ARRAY && hint == godot::PROPERTY_HINT_ARRAY_TYPE) {
        return type_from_godot_api("typedarray::" + hint_string).name;
    }
    if (type == godot::Variant::DICTIONARY && hint == godot::PROPERTY_HINT_DICTIONARY_TYPE) {
        return type_from_godot_api("typeddictionary::" + hint_string).name;
    }
    if (type == godot::Variant::OBJECT && hint == godot::PROPERTY_HINT_RESOURCE_TYPE &&
        !hint_string.empty()) {
        return hint_string;
    }
    return native_string(godot::Variant::get_type_name(type));
}

bool reflected_nil_is_variant(const godot::Dictionary& info) {
    if (!info.has("type") ||
        static_cast<std::int64_t>(info["type"]) != godot::Variant::NIL) {
        return false;
    }
    const auto usage =
        info.has("usage")
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(info["usage"]))
            : std::uint64_t{0};
    return (usage & godot::PROPERTY_USAGE_NIL_IS_VARIANT) != 0;
}

std::string reflected_name(const godot::Dictionary& info) {
    if (!info.has("name"))
        return {};
    const godot::StringName value = info["name"];
    return native_string(godot::String{value});
}

std::uint32_t reflected_method_compatibility_hash(const godot::Dictionary& method_dictionary) {
    const auto method = godot::MethodInfo::from_dict(method_dictionary);
    const bool has_return = method.return_val.type != godot::Variant::NIL ||
                            (method.return_val.usage & godot::PROPERTY_USAGE_NIL_IS_VARIANT) != 0;
    auto hash = godot::hash_murmur3_one_32(has_return ? 1U : 0U);
    hash = godot::hash_murmur3_one_32(static_cast<std::uint32_t>(method.arguments.size()), hash);
    if (has_return) {
        hash = godot::hash_murmur3_one_32(static_cast<std::uint32_t>(method.return_val.type), hash);
        if (!method.return_val.class_name.is_empty()) {
            hash = godot::hash_murmur3_one_32(
                static_cast<std::uint32_t>(method.return_val.class_name.hash()), hash);
        }
    }
    for (const auto& argument : method.arguments) {
        hash = godot::hash_murmur3_one_32(static_cast<std::uint32_t>(argument.type), hash);
        if (!argument.class_name.is_empty()) {
            hash = godot::hash_murmur3_one_32(
                static_cast<std::uint32_t>(argument.class_name.hash()), hash);
        }
    }
    hash = godot::hash_murmur3_one_32(static_cast<std::uint32_t>(method.default_arguments.size()),
                                      hash);
    for (const auto& default_argument : method.default_arguments)
        hash = godot::hash_murmur3_one_32(default_argument.hash(), hash);
    hash =
        godot::hash_murmur3_one_32((method.flags & godot::METHOD_FLAG_CONST) != 0 ? 1U : 0U, hash);
    hash =
        godot::hash_murmur3_one_32((method.flags & godot::METHOD_FLAG_VARARG) != 0 ? 1U : 0U, hash);
    return godot::hash_fmix32(hash);
}

std::vector<ExtensionBridge> reflect_extension_contracts() {
    auto* class_db = godot::ClassDBSingleton::get_singleton();
    if (!class_db)
        return {};

    const auto is_extension_class = [&](const godot::StringName& name) {
        if (name.is_empty() || !class_db->class_exists(name))
            return false;
        const auto api = class_db->class_get_api_type(name);
        return api == godot::ClassDBSingleton::API_EXTENSION ||
               api == godot::ClassDBSingleton::API_EDITOR_EXTENSION;
    };
    std::vector<ExtensionBridge> result;
    for (const auto& reflected_name_value : class_db->get_class_list()) {
        const godot::StringName class_name = reflected_name_value;
        const auto class_name_utf8 = native_string(godot::String{class_name});
        if (!is_extension_class(class_name) || class_name_utf8 == "GDPPCompiler" ||
            class_name_utf8.rfind("GDPPNative_", 0) == 0) {
            continue;
        }

        ExtensionBridgeClass contract;
        contract.gdscript_name = class_name_utf8;
        contract.runtime_only = true;
        contract.editor_only = class_db->class_get_api_type(class_name) ==
                               godot::ClassDBSingleton::API_EDITOR_EXTENSION;
        contract.members_complete = true;
        std::unordered_set<std::string> member_keys;
        godot::StringName current = class_name;
        std::unordered_set<std::string> visited;
        while (is_extension_class(current)) {
            const auto current_utf8 = native_string(godot::String{current});
            if (!visited.insert(current_utf8).second)
                break;

            const auto methods = class_db->class_get_method_list(current, true);
            for (std::int64_t index = 0; index < methods.size(); ++index) {
                const godot::Dictionary method = methods[index];
                ExtensionBridgeMember member;
                member.kind = ExtensionBridgeMemberKind::method;
                member.name = reflected_name(method);
                if (member.name.empty())
                    continue;
                const auto flags =
                    method.has("flags")
                        ? static_cast<std::uint64_t>(static_cast<std::int64_t>(method["flags"]))
                        : std::uint64_t{0};
                member.is_static = (flags & godot::METHOD_FLAG_STATIC) != 0;
                member.vararg = (flags & godot::METHOD_FLAG_VARARG) != 0;
                member.method_hash = reflected_method_compatibility_hash(method);
                member.has_method_hash = true;
                if (method.has("return")) {
                    const godot::Dictionary return_info = method["return"];
                    member.type = reflected_type_name(return_info, true);
                } else {
                    member.type = "void";
                }
                godot::Array arguments;
                if (method.has("args"))
                    arguments = method["args"];
                godot::Array defaults;
                if (method.has("default_args"))
                    defaults = method["default_args"];
                const auto required = arguments.size() - defaults.size();
                for (std::int64_t argument_index = 0; argument_index < arguments.size();
                     ++argument_index) {
                    const godot::Dictionary argument = arguments[argument_index];
                    ExtensionBridgeParameter parameter;
                    parameter.name = reflected_name(argument);
                    if (parameter.name.empty())
                        parameter.name = "arg" + std::to_string(argument_index);
                    parameter.type = reflected_type_name(argument, false);
                    parameter.has_default = argument_index >= required;
                    member.parameters.push_back(std::move(parameter));
                }
                const auto key = "m:" + member.name;
                if (member_keys.insert(key).second)
                    contract.members.push_back(std::move(member));
            }

            const auto properties = class_db->class_get_property_list(current, true);
            for (std::int64_t index = 0; index < properties.size(); ++index) {
                const godot::Dictionary property = properties[index];
                ExtensionBridgeMember member;
                member.kind = ExtensionBridgeMemberKind::property;
                member.name = reflected_name(property);
                if (member.name.empty() ||
                    (property.has("type") &&
                     static_cast<std::int64_t>(property["type"]) == godot::Variant::NIL &&
                     !reflected_nil_is_variant(property))) {
                    continue;
                }
                member.type = reflected_type_name(property, false);
                if (member.type == "int") {
                    const auto getter =
                        class_db->class_get_property_getter(current, member.name.c_str());
                    const auto getter_member = std::find_if(
                        contract.members.begin(), contract.members.end(),
                        [&](const auto& candidate) {
                            return candidate.kind == ExtensionBridgeMemberKind::method &&
                                   candidate.name == native_string(godot::String{getter});
                        });
                    if (getter_member != contract.members.end() &&
                        getter_member->type.find('.') != std::string::npos) {
                        member.type = getter_member->type;
                    }
                }
                const auto usage =
                    property.has("usage")
                        ? static_cast<std::uint64_t>(static_cast<std::int64_t>(property["usage"]))
                        : std::uint64_t{0};
                member.read_only =
                    (usage & godot::PROPERTY_USAGE_READ_ONLY) != 0 ||
                    class_db->class_get_property_setter(current, member.name.c_str()).is_empty();
                const auto key = "p:" + member.name;
                if (member_keys.insert(key).second)
                    contract.members.push_back(std::move(member));
            }

            const auto signals = class_db->class_get_signal_list(current, true);
            for (std::int64_t index = 0; index < signals.size(); ++index) {
                const godot::Dictionary signal = signals[index];
                ExtensionBridgeMember member;
                member.kind = ExtensionBridgeMemberKind::signal;
                member.name = reflected_name(signal);
                member.type = "Signal";
                if (member.name.empty())
                    continue;
                godot::Array arguments;
                if (signal.has("args"))
                    arguments = signal["args"];
                for (std::int64_t argument_index = 0; argument_index < arguments.size();
                     ++argument_index) {
                    const godot::Dictionary argument = arguments[argument_index];
                    ExtensionBridgeParameter parameter;
                    parameter.name = reflected_name(argument);
                    if (parameter.name.empty())
                        parameter.name = "arg" + std::to_string(argument_index);
                    parameter.type = reflected_type_name(argument, false);
                    member.parameters.push_back(std::move(parameter));
                }
                const auto key = "s:" + member.name;
                if (member_keys.insert(key).second)
                    contract.members.push_back(std::move(member));
            }

            const auto constants = class_db->class_get_integer_constant_list(current, true);
            for (const auto& constant_name_value : constants) {
                const godot::StringName constant_name = constant_name_value;
                if (!class_db->class_get_integer_constant_enum(current, constant_name, true)
                         .is_empty()) {
                    continue;
                }
                ExtensionBridgeMember member;
                member.kind = ExtensionBridgeMemberKind::constant;
                member.name = native_string(godot::String{constant_name});
                member.type = "int";
                member.read_only = true;
                member.is_static = true;
                member.constant_value =
                    class_db->class_get_integer_constant(current, constant_name);
                const auto key = "c:" + member.name;
                if (!member.name.empty() && member_keys.insert(key).second)
                    contract.members.push_back(std::move(member));
            }

            const auto enum_names = class_db->class_get_enum_list(current, true);
            for (const auto& enum_name_value : enum_names) {
                const godot::StringName enum_name = enum_name_value;
                const auto enum_name_utf8 = native_string(godot::String{enum_name});
                const auto already_present = std::find_if(
                    contract.enums.begin(), contract.enums.end(),
                    [&](const auto& enumeration) { return enumeration.name == enum_name_utf8; });
                if (enum_name_utf8.empty() || already_present != contract.enums.end())
                    continue;
                ExtensionBridgeEnum enumeration;
                enumeration.name = enum_name_utf8;
                enumeration.is_bitfield =
                    class_db->is_class_enum_bitfield(current, enum_name, true);
                const auto enum_constants =
                    class_db->class_get_enum_constants(current, enum_name, true);
                for (const auto& constant_name_value : enum_constants) {
                    const godot::StringName constant_name = constant_name_value;
                    enumeration.entries.push_back(
                        {native_string(godot::String{constant_name}),
                         class_db->class_get_integer_constant(current, constant_name)});
                }
                if (!enumeration.entries.empty())
                    contract.enums.push_back(std::move(enumeration));
            }
            current = class_db->get_parent_class(current);
        }
        contract.godot_base = current.is_empty() ? "Object" : native_string(godot::String{current});
        if (!current.is_empty()) {
            const auto base_api = class_db->class_get_api_type(current);
            contract.editor_only = contract.editor_only ||
                                   base_api == godot::ClassDBSingleton::API_EDITOR ||
                                   base_api == godot::ClassDBSingleton::API_EDITOR_EXTENSION;
        }

        std::sort(contract.members.begin(), contract.members.end(),
                  [](const auto& left, const auto& right) {
                      if (left.kind != right.kind)
                          return left.kind < right.kind;
                      return left.name < right.name;
                  });
        std::sort(contract.enums.begin(), contract.enums.end(),
                  [](const auto& left, const auto& right) { return left.name < right.name; });

        std::string identity = contract.gdscript_name + "\nbase:" + contract.godot_base +
                               "\neditor-only:" + (contract.editor_only ? "true\n" : "false\n");
        for (const auto& member : contract.members) {
            identity += std::to_string(static_cast<int>(member.kind)) + ":" + member.name + ":" +
                        member.type + ":" + (member.read_only ? "ro" : "rw") + ":" +
                        (member.vararg ? "vararg" : "fixed") + ":" +
                        (member.is_static ? "static" : "instance") + ":" +
                        std::to_string(member.constant_value) + ":" +
                        (member.has_method_hash ? std::to_string(member.method_hash) : "no-hash") +
                        "\n";
            for (const auto& parameter : member.parameters)
                identity += "arg:" + parameter.name + ":" + parameter.type + ":" +
                            (parameter.has_default ? "default" : "required") + "\n";
        }
        for (const auto& enumeration : contract.enums) {
            identity += "enum:" + enumeration.name + ":" +
                        (enumeration.is_bitfield ? "bitfield" : "enum") + "\n";
            for (const auto& entry : enumeration.entries)
                identity += "value:" + entry.name + ":" + std::to_string(entry.value) + "\n";
        }

        ExtensionBridge bridge;
        // Reflected contracts have no source file. Their exact ClassDB identity already enters
        // the script/build hashes; leaving the path empty prevents the native object cache from
        // treating a fictional manifest as perpetually stale.
        bridge.manifest_path.clear();
        bridge.provider = "ClassDB";
        bridge.abi = "classdb:" + contract.gdscript_name;
        bridge.contract_hash = sha256(identity);
        bridge.classes.push_back(std::move(contract));
        result.push_back(std::move(bridge));
    }
    return result;
}

godot::Dictionary invalid_version_result(const godot::String& value) {
    godot::Dictionary output;
    output["success"] = false;
    godot::PackedStringArray diagnostics;
    diagnostics.push_back("unsupported target Godot version '" + value +
                          "'; expected 4.4, 4.5, 4.6, or 4.7");
    output["diagnostics"] = diagnostics;
    return output;
}

std::filesystem::path versioned_sdk_root(const std::filesystem::path& root, GodotVersion version,
                                         NativePlatform platform, std::string_view architecture,
                                         NativeWebThreadMode web_thread_mode) {
    const auto version_root = std::filesystem::is_regular_file(root / "sdk.manifest")
                                  ? root
                                  : root / godot_version_name(version);
    const auto target_root =
        version_root / native_platform_name(platform) / std::string{architecture};
    if (platform == NativePlatform::web) {
        const auto variant =
            web_thread_mode == NativeWebThreadMode::multi_threaded ? "threads" : "nothreads";
        if (std::filesystem::is_regular_file(target_root / variant / "sdk.manifest"))
            return target_root / variant;
    }
    if (std::filesystem::is_regular_file(target_root / "sdk.manifest"))
        return target_root;
    return version_root;
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return {};
    const auto length = static_cast<int>(value.size());
    const int required =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, nullptr, 0);
    if (required <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, result.data(),
                            required) != required)
        return {};
    return result;
}

std::optional<std::wstring> windows_environment(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return std::nullopt;
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0 || copied >= required)
        return std::nullopt;
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::optional<std::filesystem::path> find_vcvars_batch(const std::filesystem::path& compiler) {
    const auto existing_file = [](const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    };
    if (const auto configured = windows_environment(L"GDPP_VCVARS_PATH")) {
        const std::filesystem::path path{*configured};
        if (existing_file(path))
            return path;
    }

    if (!compiler.empty() && compiler.has_parent_path()) {
        auto directory = std::filesystem::absolute(compiler).parent_path();
        while (!directory.empty()) {
            for (const auto& relative :
                 {std::filesystem::path{"Auxiliary/Build/vcvars64.bat"},
                  std::filesystem::path{"VC/Auxiliary/Build/vcvars64.bat"}}) {
                const auto candidate = directory / relative;
                if (existing_file(candidate))
                    return candidate;
            }
            const auto parent = directory.parent_path();
            if (parent == directory)
                break;
            directory = parent;
        }
    }

    std::vector<std::filesystem::path> roots;
    const auto append_environment_root = [&roots](const wchar_t* name) {
        if (const auto value = windows_environment(name))
            roots.emplace_back(*value);
    };
    append_environment_root(L"VSINSTALLDIR");
    append_environment_root(L"VCINSTALLDIR");
    if (const auto profile = windows_environment(L"USERPROFILE"))
        roots.emplace_back(std::filesystem::path{*profile} / "software/VS");

    const auto append_visual_studio_roots = [&roots](const wchar_t* environment_name) {
        const auto value = windows_environment(environment_name);
        if (!value)
            return;
        const auto base = std::filesystem::path{*value} / "Microsoft Visual Studio";
        for (const auto* year : {L"2026", L"2022", L"2019"}) {
            for (const auto* edition :
                 {L"BuildTools", L"Community", L"Professional", L"Enterprise", L"Preview"})
                roots.emplace_back(base / year / edition);
        }
    };
    append_visual_studio_roots(L"ProgramFiles");
    append_visual_studio_roots(L"ProgramFiles(x86)");

    for (const auto& root : roots) {
        for (const auto& relative : {std::filesystem::path{"VC/Auxiliary/Build/vcvars64.bat"},
                                     std::filesystem::path{"Auxiliary/Build/vcvars64.bat"}}) {
            const auto candidate = root / relative;
            if (existing_file(candidate))
                return candidate;
        }
    }
    return std::nullopt;
}

std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring result{L"\""};
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(backslashes, L'\\');
            result.push_back(character);
        }
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

bool is_msvc_tool(const std::filesystem::path& executable) {
    auto filename = executable.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return filename == L"cl" || filename == L"cl.exe" || filename == L"link" ||
           filename == L"link.exe";
}

int64_t execute_with_vcvars(const std::wstring& executable,
                            const std::vector<std::wstring>& arguments,
                            const std::filesystem::path& vcvars) {
    std::wstring command = L"call " + quote_windows_argument(vcvars.wstring()) + L" >nul && " +
                           quote_windows_argument(executable);
    for (std::size_t index = 1; index < arguments.size(); ++index)
        command += L" " + quote_windows_argument(arguments[index]);
    const std::vector<std::wstring> command_arguments{L"cmd.exe", L"/d", L"/s", L"/c",
                                                      std::move(command)};
    std::vector<const wchar_t*> pointers;
    pointers.reserve(command_arguments.size() + 1);
    for (const auto& argument : command_arguments)
        pointers.push_back(argument.c_str());
    pointers.push_back(nullptr);
    const auto result = _wspawnvp(_P_WAIT, L"cmd.exe", pointers.data());
    return result < 0 ? int64_t{-1} : static_cast<int64_t>(result);
}
#endif

int64_t execute_native_process(const std::string& executable,
                               const std::vector<std::string>& arguments) {
#ifdef _WIN32
    auto wide_executable = utf8_to_wide(executable);
    if (wide_executable.empty())
        return -1;
    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(arguments.size() + 1);
    wide_arguments.push_back(wide_executable);
    for (const auto& argument : arguments) {
        auto converted = utf8_to_wide(argument);
        if (!argument.empty() && converted.empty())
            return -1;
        wide_arguments.push_back(std::move(converted));
    }
    if (is_msvc_tool(std::filesystem::path{wide_executable})) {
        static std::mutex discovery_mutex;
        static std::optional<std::filesystem::path> cached_vcvars;
        static bool discovery_complete = false;
        std::optional<std::filesystem::path> vcvars;
        {
            std::lock_guard<std::mutex> lock{discovery_mutex};
            if (!discovery_complete) {
                cached_vcvars = find_vcvars_batch(std::filesystem::path{wide_executable});
                discovery_complete = true;
            }
            vcvars = cached_vcvars;
        }
        if (vcvars && !windows_environment(L"INCLUDE"))
            return execute_with_vcvars(wide_executable, wide_arguments, *vcvars);
    }
    std::vector<const wchar_t*> process_arguments;
    process_arguments.reserve(wide_arguments.size() + 1);
    for (const auto& argument : wide_arguments)
        process_arguments.push_back(argument.c_str());
    process_arguments.push_back(nullptr);
    const auto result = _wspawnvp(_P_WAIT, wide_executable.c_str(), process_arguments.data());
    return result < 0 ? int64_t{-1} : static_cast<int64_t>(result);
#else
    if (executable.empty())
        return -1;
    std::vector<char*> process_arguments;
    process_arguments.reserve(arguments.size() + 2);
    process_arguments.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments)
        process_arguments.push_back(const_cast<char*>(argument.c_str()));
    process_arguments.push_back(nullptr);
    pid_t process = 0;
    const int spawn_error = posix_spawnp(&process, executable.c_str(), nullptr, nullptr,
                                         process_arguments.data(), environ);
    if (spawn_error != 0)
        return -1;
    int status = 0;
    pid_t waited = 0;
    do {
        waited = waitpid(process, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0)
        return -1;
    if (WIFEXITED(status))
        return static_cast<int64_t>(WEXITSTATUS(status));
    if (WIFSIGNALED(status))
        return static_cast<int64_t>(128 + WTERMSIG(status));
    return -1;
#endif
}

struct NativeInvocation {
    std::string executable;
    std::vector<std::string> arguments;
    std::size_t stage{0};
};

} // namespace

void GDPPCompiler::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("compile_source", "source", "virtual_path", "target_version"),
        &GDPPCompiler::compile_source, DEFVAL("4.4"));
    godot::ClassDB::bind_method(
        godot::D_METHOD("compile_file", "source_path", "output_directory", "target_version"),
        &GDPPCompiler::compile_file, DEFVAL("4.4"));
    godot::ClassDB::bind_method(
        godot::D_METHOD("compile_project", "project_root", "output_directory", "sdk_root",
                        "compiler_executable", "target_version", "build_profile", "target_platform",
                        "target_architecture", "target_variant"),
        &GDPPCompiler::compile_project, DEFVAL("4.4"), DEFVAL("development"), DEFVAL(""),
        DEFVAL(""), DEFVAL(""));
    godot::ClassDB::bind_method(godot::D_METHOD("get_default_sdk_root"),
                                &GDPPCompiler::get_default_sdk_root);
    godot::ClassDB::bind_method(godot::D_METHOD("get_default_compiler_executable"),
                                &GDPPCompiler::get_default_compiler_executable);
    godot::ClassDB::bind_method(godot::D_METHOD("get_host_platform"),
                                &GDPPCompiler::get_host_platform);
    godot::ClassDB::bind_method(godot::D_METHOD("get_host_architecture"),
                                &GDPPCompiler::get_host_architecture);
    godot::ClassDB::bind_method(godot::D_METHOD("get_supported_godot_versions"),
                                &GDPPCompiler::get_supported_godot_versions);
    godot::ClassDB::bind_method(godot::D_METHOD("execute_project_build", "build_plan"),
                                &GDPPCompiler::execute_project_build);
    godot::ClassDB::bind_method(
        godot::D_METHOD("prune_stale_development_libraries", "current_library"),
        &GDPPCompiler::prune_stale_development_libraries);
}

int64_t GDPPCompiler::execute_build_commands(const godot::Array& commands) const {
    std::vector<NativeInvocation> invocations;
    invocations.reserve(static_cast<std::size_t>(commands.size()));
    for (int64_t index = 0; index < commands.size(); ++index) {
        const godot::Dictionary item = commands[index];
        NativeInvocation invocation;
        invocation.executable = native_string(item.get("executable", godot::String{}));
        const godot::PackedStringArray arguments =
            item.get("arguments", godot::PackedStringArray{});
        invocation.arguments.reserve(static_cast<std::size_t>(arguments.size()));
        for (int64_t argument = 0; argument < arguments.size(); ++argument)
            invocation.arguments.push_back(native_string(arguments[argument]));
        if (invocation.executable.empty())
            return -1;
        const auto stage = static_cast<int64_t>(item.get("stage", int64_t{0}));
        if (stage < 0)
            return -1;
        invocation.stage = static_cast<std::size_t>(stage);
        invocations.push_back(std::move(invocation));
    }
    if (invocations.empty())
        return 0;

    std::stable_sort(invocations.begin(), invocations.end(),
                     [](const auto& left, const auto& right) { return left.stage < right.stage; });
    for (std::size_t begin = 0; begin < invocations.size();) {
        std::size_t end = begin + 1;
        while (end < invocations.size() && invocations[end].stage == invocations[begin].stage)
            ++end;
        const auto count = end - begin;
        const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
        const auto worker_count =
            std::min(count, static_cast<std::size_t>(std::min(hardware_threads, 8U)));
        std::atomic<std::size_t> next{begin};
        std::atomic<int64_t> first_error{0};
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&]() {
                while (first_error.load() == 0) {
                    const auto index = next.fetch_add(1);
                    if (index >= end)
                        return;
                    const auto& invocation = invocations[index];
                    const auto exit_code =
                        execute_native_process(invocation.executable, invocation.arguments);
                    if (exit_code != 0) {
                        int64_t expected = 0;
                        (void)first_error.compare_exchange_strong(expected, exit_code);
                    }
                }
            });
        }
        for (auto& worker : workers)
            worker.join();
        if (first_error.load() != 0)
            return first_error.load();
        begin = end;
    }
    return 0;
}

godot::Dictionary GDPPCompiler::execute_project_build(const godot::Dictionary& build_plan) const {
    godot::Dictionary output;
    output["success"] = false;
    output["exit_code"] = int64_t{-1};
    output["cleanup_success"] = true;
    output["removed_count"] = int64_t{0};
    godot::PackedStringArray diagnostics =
        build_plan.get("diagnostics", godot::PackedStringArray{});

    if (!static_cast<bool>(build_plan.get("success", false))) {
        output["diagnostics"] = diagnostics;
        return output;
    }

    const godot::Array commands = build_plan.get("build_commands", godot::Array{});
    const auto exit_code = execute_build_commands(commands);
    output["exit_code"] = exit_code;
    if (exit_code != 0) {
        diagnostics.push_back("C++ toolchain failed with exit code " +
                              godot::String::num_int64(exit_code));
        output["diagnostics"] = diagnostics;
        return output;
    }

    const godot::String output_library = build_plan.get("output_library", godot::String{});
    auto* settings = godot::ProjectSettings::get_singleton();
    const auto output_path = native_string(settings->globalize_path(output_library));
    const godot::String pending_output = build_plan.get("pending_output_library", godot::String{});
    if (!pending_output.is_empty()) {
        const auto pending_path = native_string(settings->globalize_path(pending_output));
        std::string commit_diagnostic;
        if (!commit_xcframework(path_from_utf8(pending_path), path_from_utf8(output_path),
                                commit_diagnostic)) {
            diagnostics.push_back(godot::String{commit_diagnostic.c_str()});
            output["diagnostics"] = diagnostics;
            return output;
        }
    }
    std::error_code file_error;
    const auto output_filesystem_path = path_from_utf8(output_path);
    const bool artifact_exists =
        std::filesystem::is_regular_file(output_filesystem_path, file_error) ||
        is_xcframework(output_filesystem_path);
    if (output_path.empty() || !artifact_exists || file_error) {
        diagnostics.push_back("native build completed without producing the planned library '" +
                              output_library + "'");
        output["diagnostics"] = diagnostics;
        return output;
    }

    const auto profile =
        native_string(static_cast<godot::String>(build_plan.get("build_profile", godot::String{})));
    if (profile == "development") {
        const auto cleanup = gdpp::prune_stale_development_libraries(output_filesystem_path);
        output["cleanup_success"] = cleanup.success;
        output["removed_count"] = static_cast<int64_t>(cleanup.removed_count);
        for (const auto& diagnostic : cleanup.diagnostics)
            diagnostics.push_back(godot::String{diagnostic.c_str()});
    }

    output["success"] = true;
    output["diagnostics"] = diagnostics;
    return output;
}

godot::Dictionary
GDPPCompiler::prune_stale_development_libraries(const godot::String& current_library) const {
    auto* settings = godot::ProjectSettings::get_singleton();
    const auto path = native_string(settings->globalize_path(current_library));
    const auto cleanup = gdpp::prune_stale_development_libraries(path);
    godot::Dictionary output;
    output["success"] = cleanup.success;
    output["removed_count"] = static_cast<int64_t>(cleanup.removed_count);
    godot::PackedStringArray diagnostics;
    for (const auto& diagnostic : cleanup.diagnostics)
        diagnostics.push_back(godot::String{diagnostic.c_str()});
    output["diagnostics"] = diagnostics;
    return output;
}

godot::Dictionary GDPPCompiler::compile_project(
    const godot::String& project_root, const godot::String& output_directory,
    const godot::String& sdk_root, const godot::String& compiler_executable,
    const godot::String& target_version, const godot::String& build_profile,
    const godot::String& target_platform, const godot::String& target_architecture,
    const godot::String& target_variant) const {
    const auto version = parse_godot_version(native_string(target_version));
    if (!version)
        return invalid_version_result(target_version);
    const auto profile_value = native_string(build_profile);
    const auto profile = parse_native_build_profile(profile_value);
    if (!profile) {
        godot::Dictionary output;
        output["success"] = false;
        godot::PackedStringArray diagnostics;
        diagnostics.push_back("unsupported build profile '" + build_profile +
                              "'; expected development, debug, or release");
        output["diagnostics"] = diagnostics;
        return output;
    }
    const auto platform_value = target_platform.is_empty() ? native_platform_name(native_platform())
                                                           : native_string(target_platform);
    const auto platform = parse_native_platform(platform_value);
    if (!platform) {
        godot::Dictionary output;
        output["success"] = false;
        godot::PackedStringArray diagnostics;
        diagnostics.push_back("unsupported native platform '" + target_platform +
                              "'; expected macos, linux, windows, android, ios, or web");
        output["diagnostics"] = diagnostics;
        return output;
    }
    const auto architecture = target_architecture.is_empty() ? std::string{GDPP_ARCH}
                                                             : native_string(target_architecture);
    NativeWebThreadMode web_thread_mode = NativeWebThreadMode::not_applicable;
    const auto variant = native_string(target_variant);
    if (*platform == NativePlatform::web) {
        if (variant == "threads")
            web_thread_mode = NativeWebThreadMode::multi_threaded;
        else if (variant == "nothreads")
            web_thread_mode = NativeWebThreadMode::single_threaded;
        else {
            godot::Dictionary output;
            output["success"] = false;
            godot::PackedStringArray diagnostics;
            diagnostics.push_back("Web target variant must be 'threads' or 'nothreads'");
            output["diagnostics"] = diagnostics;
            return output;
        }
    } else if (!variant.empty()) {
        godot::Dictionary output;
        output["success"] = false;
        godot::PackedStringArray diagnostics;
        diagnostics.push_back("target variant is only valid for the Web platform");
        output["diagnostics"] = diagnostics;
        return output;
    }
    auto* settings = godot::ProjectSettings::get_singleton();
    ProjectCompileOptions options;
    options.project_root = path_from_utf8(native_string(settings->globalize_path(project_root)));
    options.output_directory =
        path_from_utf8(native_string(settings->globalize_path(output_directory)));
    options.sdk_root =
        versioned_sdk_root(path_from_utf8(native_string(settings->globalize_path(sdk_root))),
                           *version, *platform, architecture, web_thread_mode);
    options.reflected_extension_bridges = reflect_extension_contracts();
    options.compiler.target_version = *version;
    options.generate_cmake = false;
    const auto development_descriptor = options.output_directory / "gdpp_project.gdextension";
    const auto preserved_development_descriptor = *profile == NativeBuildProfile::development
                                                      ? std::optional<std::string>{}
                                                      : read_file(development_descriptor);
    const ProjectCompiler compiler;
    const auto result = compiler.compile(options);
    std::string descriptor_additional_sections;
    if (result.success && *profile == NativeBuildProfile::development) {
        if (const auto descriptor = read_file(result.extension_descriptor)) {
            const auto additional = descriptor->find("\n[icons]\n");
            if (additional != std::string::npos)
                descriptor_additional_sections = descriptor->substr(additional + 1);
        }
    }
    bool descriptor_restore_failed = false;
    if (result.success && preserved_development_descriptor &&
        !write_file_atomic(development_descriptor, *preserved_development_descriptor)) {
        descriptor_restore_failed = true;
    }

    godot::Dictionary output;
    output["success"] = result.success && !descriptor_restore_failed;
    output["compiled_count"] = static_cast<int64_t>(result.compiled_count);
    output["cache_hit_count"] = static_cast<int64_t>(result.cache_hit_count);
    output["removed_count"] = static_cast<int64_t>(result.removed_count);
    godot::PackedStringArray scripts;
    godot::PackedStringArray abstract_scripts;
    godot::PackedStringArray editor_only_scripts;
    godot::Dictionary script_classes;
    godot::Dictionary attached_script_bases;
    for (const auto& script : result.scripts) {
        const auto relative_path = generic_path_to_utf8(script.relative_path);
        scripts.push_back(godot::String::utf8(relative_path.c_str()));
        const auto resource_path = "res://" + relative_path;
        script_classes[godot::String{resource_path.c_str()}] =
            godot::String{script.class_name.c_str()};
        if (script.is_attached) {
            attached_script_bases[godot::String{resource_path.c_str()}] =
                godot::String{script.external_base_name.c_str()};
        }
        if (script.is_abstract)
            abstract_scripts.push_back(godot::String{resource_path.c_str()});
        if (script.is_editor_only)
            editor_only_scripts.push_back(godot::String{resource_path.c_str()});
    }
    output["scripts"] = scripts;
    output["abstract_scripts"] = abstract_scripts;
    output["editor_only_scripts"] = editor_only_scripts;
    output["script_classes"] = script_classes;
    output["attached_script_bases"] = attached_script_bases;
    godot::PackedStringArray diagnostics;
    for (const auto& item : result.diagnostics) {
        const auto message = generic_path_to_utf8(item.path) + ":" +
                             std::to_string(item.diagnostic.span.begin.line) + ":" +
                             std::to_string(item.diagnostic.span.begin.column) + ": " +
                             item.diagnostic.code + ": " + item.diagnostic.message;
        diagnostics.push_back(godot::String{message.c_str()});
    }
    if (descriptor_restore_failed) {
        diagnostics.push_back(
            "cannot restore the development GDExtension descriptor after a distribution build");
    }
    output["diagnostics"] = diagnostics;
    if (result.success && !descriptor_restore_failed) {
        output["extension_descriptor"] =
            godot::String::utf8(generic_path_to_utf8(result.extension_descriptor).c_str());
        output["build_id"] = godot::String{result.build_id.c_str()};
        NativeBuildOptions build_options;
        build_options.project_output_directory = options.output_directory;
        build_options.binary_output_directory = result.native_library_directory;
        build_options.sdk_root = options.sdk_root;
        build_options.compiler_executable = native_string(compiler_executable);
        build_options.platform = *platform;
        build_options.architecture = architecture;
        build_options.profile = *profile;
        build_options.web_thread_mode = web_thread_mode;
        build_options.target_version = *version;
        const NativeBuilder builder;
        const auto plan = builder.plan(build_options);
        if (!plan.success) {
            output["success"] = false;
            for (const auto& message : plan.diagnostics)
                diagnostics.push_back(godot::String{message.c_str()});
            output["diagnostics"] = diagnostics;
            return output;
        }
        if (*profile == NativeBuildProfile::development) {
            const auto output_path =
                godot::String::utf8(generic_path_to_utf8(plan.output_library).c_str());
            const auto resource_path = native_string(settings->localize_path(output_path));
            const auto descriptor = native_development_extension_descriptor(
                *version, *platform, architecture, resource_path, web_thread_mode,
                descriptor_additional_sections, attached_script_bases.is_empty());
            if (!write_file_atomic(result.extension_descriptor, descriptor)) {
                output["success"] = false;
                diagnostics.push_back(
                    "cannot atomically update the architecture-specific development extension "
                    "descriptor: " +
                    godot::String::utf8(generic_path_to_utf8(result.extension_descriptor).c_str()));
                output["diagnostics"] = diagnostics;
                return output;
            }
            output["development_library_resource_path"] = godot::String{resource_path.c_str()};
        }
        godot::Array commands;
        for (const auto& command : plan.commands) {
            godot::Dictionary item;
            item["executable"] = godot::String{command.executable.c_str()};
            godot::PackedStringArray arguments;
            for (const auto& argument : command.arguments)
                arguments.push_back(godot::String{argument.c_str()});
            item["arguments"] = arguments;
            item["stage"] = static_cast<int64_t>(command.stage);
            commands.push_back(item);
        }
        output["build_commands"] = commands;
        output["native_up_to_date"] = plan.up_to_date;
        output["output_library"] =
            godot::String::utf8(generic_path_to_utf8(plan.output_library).c_str());
        if (!plan.pending_output_library.empty()) {
            output["pending_output_library"] =
                godot::String::utf8(generic_path_to_utf8(plan.pending_output_library).c_str());
        }
        output["build_profile"] = build_profile;
        output["target_platform"] = godot::String{platform_value.c_str()};
        output["target_architecture"] = godot::String{architecture.c_str()};
    }
    return output;
}

godot::String GDPPCompiler::get_default_sdk_root() const { return godot::String{GDPP_SDK_ROOT}; }

godot::String GDPPCompiler::get_default_compiler_executable() const { return default_compiler(); }

godot::String GDPPCompiler::get_host_platform() const {
    return godot::String{native_platform_name(native_platform()).c_str()};
}

godot::String GDPPCompiler::get_host_architecture() const {
    return godot::String{host_process_architecture().c_str()};
}

godot::PackedStringArray GDPPCompiler::get_supported_godot_versions() const {
    godot::PackedStringArray versions;
    versions.push_back("4.4");
    versions.push_back("4.5");
    versions.push_back("4.6");
    versions.push_back("4.7");
    return versions;
}

godot::Dictionary GDPPCompiler::compile_source(const godot::String& source,
                                               const godot::String& virtual_path,
                                               const godot::String& target_version) const {
    const auto version = parse_godot_version(native_string(target_version));
    if (!version)
        return invalid_version_result(target_version);
    const auto path = native_string(virtual_path);
    const auto text = native_string(source);
    const SourceFile source_file{path, text};
    const Compiler compiler;
    CompileOptions options;
    options.target_version = *version;
    return result_dictionary(compiler.compile(path, text, options), source_file);
}

godot::Dictionary GDPPCompiler::compile_file(const godot::String& source_path,
                                             const godot::String& output_directory,
                                             const godot::String& target_version) const {
    const auto version = parse_godot_version(native_string(target_version));
    if (!version)
        return invalid_version_result(target_version);
    auto* settings = godot::ProjectSettings::get_singleton();
    const auto native_source_name = native_string(settings->globalize_path(source_path));
    const auto native_output_name = native_string(settings->globalize_path(output_directory));
    const auto native_source_path = path_from_utf8(native_source_name);
    const auto native_output_path = path_from_utf8(native_output_name);

    std::ifstream input{native_source_path, std::ios::binary};
    godot::Dictionary failure;
    if (!input) {
        failure["success"] = false;
        godot::PackedStringArray diagnostics;
        diagnostics.push_back("cannot open source file: " + source_path);
        failure["diagnostics"] = diagnostics;
        return failure;
    }
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const SourceFile source_file{native_source_name, text};
    const Compiler compiler;
    CompileOptions options;
    options.target_version = *version;
    const auto result = compiler.compile(native_source_name, text, options);
    auto output = result_dictionary(result, source_file);
    if (!result.success)
        return output;

    std::error_code error;
    std::filesystem::create_directories(native_output_path, error);
    if (error ||
        !write_file(native_output_path / result.unit.header_file_name, result.unit.header) ||
        !write_file(native_output_path / result.unit.source_file_name, result.unit.source)) {
        output["success"] = false;
        auto diagnostics = static_cast<godot::PackedStringArray>(output["diagnostics"]);
        diagnostics.push_back("cannot write generated files to: " + output_directory);
        output["diagnostics"] = diagnostics;
    }
    return output;
}

} // namespace gdpp::extension
