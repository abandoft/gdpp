#include "gdpp/project/native_builder.hpp"

#include "gdpp/support/path_utf8.hpp"
#include "gdpp/support/sha256.hpp"
#include "gdpp/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace gdpp {
namespace {

constexpr std::string_view native_build_revision{"12"};

struct BridgeBuildInputs {
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> link_libraries;
    std::vector<std::filesystem::path> manifests;
};

std::string platform_name(NativePlatform platform);

std::string object_extension(NativePlatform platform) {
    return platform == NativePlatform::windows ? ".obj" : ".o";
}

std::string read_build_id(const std::filesystem::path& output) {
    std::ifstream input{output / "build_id.txt"};
    std::string value;
    input >> value;
    if (value.size() != 16 || !std::all_of(value.begin(), value.end(), [](const char character) {
            return std::isxdigit(static_cast<unsigned char>(character)) != 0;
        })) {
        return {};
    }
    return value;
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::optional<BridgeBuildInputs> read_bridge_lock(const std::filesystem::path& path,
                                                  const NativeBuildOptions& options,
                                                  std::vector<std::string>& diagnostics) {
    if (!std::filesystem::is_regular_file(path))
        return BridgeBuildInputs{};
    std::ifstream input{path};
    std::string magic;
    std::string version;
    if (!(input >> magic >> version) || magic != "GDPP_BRIDGE_LOCK" || version != "1") {
        diagnostics.push_back("missing or incompatible third-party bridge lock: " +
                              path_to_utf8(path));
        return std::nullopt;
    }
    BridgeBuildInputs result;
    std::string token;
    std::string current_abi;
    std::filesystem::path current_manifest;
    bool current_matched = true;
    const auto finish_bridge = [&]() {
        if (!current_abi.empty() && !current_matched) {
            diagnostics.push_back("third-party bridge has no target for " +
                                  platform_name(options.platform) + "/" + options.architecture +
                                  "/" + native_build_profile_name(options.profile) + ": " +
                                  path_to_utf8(current_manifest));
        }
    };
    while (input >> token) {
        if (token == "bridge") {
            finish_bridge();
            std::string manifest_value;
            input >> std::quoted(current_abi) >> std::quoted(manifest_value);
            current_manifest = path_from_utf8(manifest_value);
            current_matched = false;
            if (!current_manifest.empty())
                result.manifests.push_back(current_manifest);
            continue;
        }
        if (token == "runtime") {
            if (current_abi.empty()) {
                diagnostics.push_back("orphan runtime record in third-party bridge lock");
                return std::nullopt;
            }
            current_matched = true;
            continue;
        }
        if (token != "target") {
            diagnostics.push_back("invalid record in third-party bridge lock: " + token);
            return std::nullopt;
        }
        std::string platform;
        std::string architecture;
        std::string profile;
        std::size_t include_count = 0;
        if (!(input >> std::quoted(platform) >> std::quoted(architecture) >> std::quoted(profile) >>
              include_count)) {
            diagnostics.push_back("truncated third-party bridge target record");
            return std::nullopt;
        }
        std::vector<std::filesystem::path> includes(include_count);
        for (auto& include : includes) {
            std::string value;
            if (!(input >> std::quoted(value)))
                return std::nullopt;
            include = path_from_utf8(value);
        }
        std::size_t library_count = 0;
        if (!(input >> library_count))
            return std::nullopt;
        std::vector<std::filesystem::path> libraries(library_count);
        for (auto& library : libraries) {
            std::string value;
            if (!(input >> std::quoted(value)))
                return std::nullopt;
            library = path_from_utf8(value);
        }
        if (platform == platform_name(options.platform) && architecture == options.architecture &&
            profile == native_build_profile_name(options.profile)) {
            current_matched = true;
            result.include_directories.insert(result.include_directories.end(), includes.begin(),
                                              includes.end());
            result.link_libraries.insert(result.link_libraries.end(), libraries.begin(),
                                         libraries.end());
        }
    }
    finish_bridge();
    if (!input.eof()) {
        diagnostics.push_back("cannot parse third-party bridge lock: " + path_to_utf8(path));
        return std::nullopt;
    }
    return diagnostics.empty() ? std::optional{std::move(result)} : std::nullopt;
}

std::optional<bool> write_file_if_changed(const std::filesystem::path& path,
                                          const std::string& content) {
    if (const auto existing = read_file(path); existing && *existing == content)
        return false;
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output.good())
            return std::nullopt;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (!error)
        return true;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return std::nullopt;
    }
    return true;
}

std::string safe_stem(const std::filesystem::path& path) {
    auto value = path_to_utf8(path.filename());
    for (char& character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0)
            character = '_';
    }
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string platform_name(NativePlatform platform) {
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

std::string web_thread_mode_name(NativeWebThreadMode mode) {
    if (mode == NativeWebThreadMode::multi_threaded)
        return "threads";
    if (mode == NativeWebThreadMode::single_threaded)
        return "nothreads";
    return "none";
}

bool validate_manifest(const NativeBuildOptions& options, std::vector<std::string>& diagnostics) {
    const auto path = options.sdk_root / "sdk.manifest";
    std::ifstream input{path};
    std::string magic;
    std::string format;
    if (!(input >> magic >> format) || magic != "GDPP_SDK" ||
        format != std::to_string(GDPP_NATIVE_SDK_SCHEMA)) {
        diagnostics.push_back("native SDK format is missing or incompatible: expected GDPP_SDK " +
                              std::to_string(GDPP_NATIVE_SDK_SCHEMA) + " at " + path_to_utf8(path) +
                              "; reinstall the SDK packaged with this GDPP compiler");
        return false;
    }
    std::string key;
    std::string value;
    std::string platform;
    std::string architecture;
    std::string api;
    std::string profiles;
    std::string runtime_abi;
    std::string runtime_header_sha256;
    std::string runtime_source_sha256;
    std::string web_threads;
    std::string source_paths;
    std::string ios_deployment_target;
    std::string ios_slices;
    std::string platform_minimum;
    std::string android_api_level;
    while (input >> key >> value) {
        if (key == "platform")
            platform = value;
        else if (key == "arch")
            architecture = value;
        else if (key == "api")
            api = value;
        else if (key == "profiles")
            profiles = value;
        else if (key == "runtime_abi")
            runtime_abi = value;
        else if (key == "runtime_header_sha256")
            runtime_header_sha256 = value;
        else if (key == "runtime_source_sha256")
            runtime_source_sha256 = value;
        else if (key == "web_threads")
            web_threads = value;
        else if (key == "source_paths")
            source_paths = value;
        else if (key == "ios_deployment_target")
            ios_deployment_target = value;
        else if (key == "ios_slices")
            ios_slices = value;
        else if (key == "platform_minimum")
            platform_minimum = value;
        else if (key == "android_api_level")
            android_api_level = value;
    }
    if (platform != platform_name(options.platform))
        diagnostics.push_back("native SDK platform mismatch: expected " +
                              platform_name(options.platform) + ", package contains " + platform);
    const bool architecture_compatible =
        architecture == options.architecture ||
        (options.platform == NativePlatform::macos && architecture == "universal" &&
         (options.architecture == "arm64" || options.architecture == "x86_64"));
    if (!architecture_compatible)
        diagnostics.push_back("native SDK architecture mismatch: expected " + options.architecture +
                              ", package contains " + architecture);
    const std::string expected_minimum = options.platform == NativePlatform::windows ? "Windows_10"
                                         : options.platform == NativePlatform::macos ? "macOS_11.0"
                                         : options.platform == NativePlatform::linux
                                             ? "Ubuntu_22.04"
                                         : options.platform == NativePlatform::android ? "Android_9"
                                         : options.platform == NativePlatform::ios     ? "iOS_16.0"
                                                                                       : "none";
    if (platform_minimum != expected_minimum) {
        diagnostics.push_back(
            "native SDK platform minimum mismatch: expected " + expected_minimum +
            ", package contains " +
            (platform_minimum.empty() ? std::string{"<missing>"} : platform_minimum));
    }
    if (options.platform == NativePlatform::android && android_api_level != "28") {
        diagnostics.push_back(
            "native Android SDK API baseline mismatch: expected 28, package "
            "contains " +
            (android_api_level.empty() ? std::string{"<missing>"} : android_api_level));
    }
    if (options.platform == NativePlatform::web) {
        const auto expected_threads = web_thread_mode_name(options.web_thread_mode);
        if (web_threads != expected_threads) {
            diagnostics.push_back("native SDK Web thread mode mismatch: expected " +
                                  expected_threads + ", package contains " +
                                  (web_threads.empty() ? std::string{"<missing>"} : web_threads));
        }
        if (source_paths != "mapped") {
            diagnostics.emplace_back(
                "native Web SDK does not guarantee reproducible source-path mapping; "
                "reinstall the matching Web target pack");
        }
    }
    if (options.platform == NativePlatform::ios) {
        if (ios_deployment_target != "16.0")
            diagnostics.emplace_back(
                "native iOS SDK deployment target must match the commercial iOS 16.0 baseline");
        const auto slices = "," + ios_slices + ",";
        for (const auto required : {"device-arm64", "simulator-arm64", "simulator-x86_64"}) {
            if (slices.find("," + std::string{required} + ",") == std::string::npos) {
                diagnostics.push_back("native iOS SDK is missing required slice '" +
                                      std::string{required} + "'");
            }
        }
        if (source_paths != "mapped") {
            diagnostics.emplace_back(
                "native iOS SDK does not guarantee reproducible source-path mapping; "
                "reinstall the matching iOS target pack");
        }
    }
    if (api != godot_version_name(options.target_version))
        diagnostics.push_back("native SDK Godot API mismatch: expected " +
                              std::string{godot_version_name(options.target_version)} +
                              ", package contains " + api);
    if (profiles.empty()) {
        diagnostics.emplace_back("native SDK manifest does not declare supported build profiles");
    } else {
        const std::string expected = native_build_profile_name(options.profile);
        const auto padded = "," + profiles + ",";
        if (padded.find("," + expected + ",") == std::string::npos) {
            diagnostics.push_back("native SDK does not support the required '" + expected +
                                  "' build profile");
        }
    }
    const auto expected_runtime_abi = std::to_string(GDPP_NATIVE_RUNTIME_ABI);
    if (runtime_abi != expected_runtime_abi) {
        diagnostics.push_back("native SDK runtime ABI mismatch: compiler requires " +
                              expected_runtime_abi + ", package contains " +
                              (runtime_abi.empty() ? std::string{"<missing>"} : runtime_abi) +
                              "; reinstall the matching GDPP SDK");
    }
    if (runtime_header_sha256 != GDPP_NATIVE_RUNTIME_HEADER_SHA256 ||
        runtime_source_sha256 != GDPP_NATIVE_RUNTIME_SOURCE_SHA256) {
        diagnostics.emplace_back(
            "native SDK runtime contract does not match this GDPP compiler; reinstall the "
            "matching plugin package");
    }

    const auto verify_runtime_file = [&](const std::filesystem::path& runtime_path,
                                         const std::string& declared_hash,
                                         const std::string_view label) {
        const auto content = read_file(runtime_path);
        if (!content) {
            diagnostics.push_back("missing native SDK " + std::string{label} + ": " +
                                  path_to_utf8(runtime_path));
        } else if (sha256(*content) != declared_hash) {
            diagnostics.push_back("native SDK " + std::string{label} +
                                  " failed integrity validation: " + path_to_utf8(runtime_path));
        }
    };
    verify_runtime_file(options.sdk_root / "include/gdpp/runtime/variant_ops.hpp",
                        runtime_header_sha256, "runtime header");
    verify_runtime_file(options.sdk_root / "src/runtime/variant_ops.cpp", runtime_source_sha256,
                        "runtime source");
    return diagnostics.empty();
}

std::string manifest_value(const std::filesystem::path& manifest, std::string_view wanted_key) {
    std::ifstream input{manifest};
    std::string key;
    std::string value;
    while (input >> key >> value) {
        if (key == wanted_key)
            return value;
    }
    return {};
}

std::filesystem::path find_binding_library(const std::filesystem::path& directory,
                                           const NativeBuildOptions& options,
                                           const std::string& target) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        return {};
    std::vector<std::filesystem::path> candidates;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file())
            continue;
        const auto extension = iterator->path().extension();
        const auto filename = iterator->path().filename().string();
        const bool thread_variant_matches =
            options.platform != NativePlatform::web ||
            (options.web_thread_mode == NativeWebThreadMode::single_threaded
                 ? filename.find(".nothreads.") != std::string::npos
                 : filename.find(".nothreads.") == std::string::npos);
        if (((options.platform == NativePlatform::windows && extension == ".lib") ||
             (options.platform != NativePlatform::windows && extension == ".a")) &&
            filename.find("." + target + ".") != std::string::npos && thread_variant_matches) {
            candidates.push_back(iterator->path());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

bool older_than(const std::filesystem::path& output,
                const std::vector<std::filesystem::path>& inputs) {
    std::error_code error;
    const auto output_time = std::filesystem::last_write_time(output, error);
    if (error)
        return true;
    for (const auto& input : inputs) {
        const auto input_time = std::filesystem::last_write_time(input, error);
        if (error || input_time > output_time)
            return true;
    }
    return false;
}

void append_include_arguments(std::vector<std::string>& arguments,
                              const std::vector<std::filesystem::path>& includes,
                              NativePlatform platform) {
    for (const auto& include : includes) {
        if (platform == NativePlatform::windows)
            arguments.push_back("/I" + path_to_utf8(include));
        else {
            arguments.emplace_back("-I");
            arguments.push_back(path_to_utf8(include));
        }
    }
}

void append_macos_architecture_arguments(std::vector<std::string>& arguments,
                                         const NativeBuildOptions& options) {
    if (options.platform != NativePlatform::macos)
        return;
    arguments.emplace_back("-mmacosx-version-min=11.0");
    if (options.architecture == "universal") {
        arguments.insert(arguments.end(), {"-arch", "arm64", "-arch", "x86_64"});
    } else if (options.architecture == "arm64" || options.architecture == "x86_64") {
        arguments.insert(arguments.end(), {"-arch", options.architecture});
    }
}

void append_android_target_arguments(std::vector<std::string>& arguments,
                                     const NativeBuildOptions& options) {
    if (options.platform != NativePlatform::android)
        return;
    const auto triple =
        options.architecture == "arm64" ? "aarch64-linux-android" : "x86_64-linux-android";
    // Keep this baseline aligned with the packaged Android godot-cpp SDK and the documented
    // Android 9 minimum. A single fixed value keeps object-cache signatures deterministic.
    arguments.push_back("--target=" + std::string{triple} + "28");
}

using ReproduciblePathMapping = std::pair<std::string, std::string>;

std::optional<std::string> read_environment_variable(const char* name) {
#if defined(_MSC_VER)
    char* raw_value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&raw_value, &value_size, name) != 0 || raw_value == nullptr)
        return std::nullopt;
    const std::unique_ptr<char, decltype(&std::free)> value{raw_value, &std::free};
    return std::string{value.get(), value_size == 0 ? 0 : value_size - 1};
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>{value};
#endif
}

void add_reproducible_path_mapping(std::vector<ReproduciblePathMapping>& mappings,
                                   const std::filesystem::path& source, std::string replacement) {
    if (source.empty())
        return;
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(source, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(source, error).lexically_normal();
    }
    if (error || normalized.empty() || normalized == normalized.root_path())
        return;
    const auto value = path_to_utf8(normalized);
    const auto existing = std::find_if(mappings.begin(), mappings.end(),
                                       [&](const auto& mapping) { return mapping.first == value; });
    if (existing == mappings.end())
        mappings.emplace_back(value, std::move(replacement));
}

std::optional<std::filesystem::path> resolve_compiler_path(std::string_view executable) {
    std::filesystem::path value{executable};
    std::error_code error;
    if (value.is_absolute() || value.has_parent_path()) {
        auto resolved = std::filesystem::weakly_canonical(value, error);
        return error ? std::nullopt : std::optional<std::filesystem::path>{resolved};
    }
    const auto path_environment = read_environment_variable("PATH");
    if (!path_environment)
        return std::nullopt;
#if defined(_WIN32)
    constexpr char path_separator = ';';
    const std::vector<std::string> suffixes{"", ".exe", ".bat", ".cmd"};
#else
    constexpr char path_separator = ':';
    const std::vector<std::string> suffixes{""};
#endif
    std::istringstream paths{*path_environment};
    std::string directory;
    while (std::getline(paths, directory, path_separator)) {
        for (const auto& suffix : suffixes) {
            const auto candidate =
                std::filesystem::path{directory} / (std::string{executable} + suffix);
            if (!std::filesystem::is_regular_file(candidate, error)) {
                error.clear();
                continue;
            }
            auto resolved = std::filesystem::weakly_canonical(candidate, error);
            if (!error)
                return resolved;
            error.clear();
        }
    }
    return std::nullopt;
}

std::vector<ReproduciblePathMapping> reproducible_path_mappings(const NativeBuildOptions& options) {
    std::vector<ReproduciblePathMapping> mappings;
    add_reproducible_path_mapping(mappings, options.project_output_directory, "/gdpp/project");
    add_reproducible_path_mapping(mappings, options.sdk_root, "/gdpp/sdk");
    if (options.platform == NativePlatform::web) {
        for (const auto& [name, replacement] :
             {std::pair{"EM_CACHE", "/gdpp/toolchain/cache"},
              std::pair{"EMSDK", "/gdpp/toolchain/emsdk"},
              std::pair{"EMSCRIPTEN_ROOT", "/gdpp/toolchain/emscripten"}}) {
            if (const auto value = read_environment_variable(name); value && !value->empty())
                add_reproducible_path_mapping(mappings, *value, replacement);
        }
        if (const auto compiler = resolve_compiler_path(options.compiler_executable)) {
            add_reproducible_path_mapping(mappings, compiler->parent_path(),
                                          "/gdpp/toolchain/compiler");
        }
    }
    std::sort(mappings.begin(), mappings.end(), [](const auto& left, const auto& right) {
        return left.first.size() > right.first.size();
    });
    return mappings;
}

void append_reproducible_path_arguments(std::vector<std::string>& arguments,
                                        const NativeBuildOptions& options) {
    for (const auto& [source, replacement] : reproducible_path_mappings(options)) {
        if (options.platform == NativePlatform::windows)
            arguments.push_back("/pathmap:" + source + "=" + replacement);
        else
            arguments.push_back("-ffile-prefix-map=" + source + "=" + replacement);
    }
}

NativeBuildCommand compile_command(const NativeBuildOptions& options,
                                   const std::filesystem::path& source,
                                   const std::filesystem::path& object,
                                   const std::vector<std::filesystem::path>& includes) {
    NativeBuildCommand command;
    command.executable = options.compiler_executable;
    command.working_directory = options.project_output_directory;
    auto& arguments = command.arguments;
    if (options.platform == NativePlatform::windows) {
        // Keep these ABI and feature switches aligned with godot-cpp's exported
        // Windows target settings. In particular, TYPED_METHOD_BIND avoids MSVC's
        // incompatible pointer-to-member representation for generated script types,
        // while /MT matches the compiler-only SDK's statically linked CRT.
        arguments = {"/nologo",
                     "/std:c++17",
                     "/utf-8",
                     "/MT",
                     "/EHsc",
                     "/DGDEXTENSION",
                     "/DTHREADS_ENABLED",
                     "/DWINDOWS_ENABLED",
                     "/D_WIN32_WINNT=0x0A00",
                     "/DWINVER=0x0A00",
                     "/DTYPED_METHOD_BIND",
                     "/D_HAS_EXCEPTIONS=0",
                     "/DNOMINMAX"};
        arguments.push_back(options.profile == NativeBuildProfile::debug ? "/Od" : "/O2");
        if (options.profile != NativeBuildProfile::release)
            arguments.emplace_back("/DDEBUG_ENABLED");
        else {
            arguments.emplace_back("/DNDEBUG");
            arguments.emplace_back("/Gy");
            arguments.emplace_back("/Gw");
        }
        append_reproducible_path_arguments(arguments, options);
        append_include_arguments(arguments, includes, options.platform);
        arguments.emplace_back("/c");
        arguments.push_back(path_to_utf8(source));
        arguments.push_back("/Fo" + path_to_utf8(object));
    } else {
        arguments = {"-std=c++17", "-fPIC", "-fno-exceptions", "-DGDEXTENSION"};
        if (options.platform != NativePlatform::web ||
            options.web_thread_mode == NativeWebThreadMode::multi_threaded) {
            arguments.emplace_back("-DTHREADS_ENABLED");
        }
        if (options.platform == NativePlatform::android)
            arguments.insert(arguments.end(), {"-DANDROID_ENABLED", "-DUNIX_ENABLED"});
        if (options.platform == NativePlatform::web) {
            arguments.insert(arguments.end(), {"-DWEB_ENABLED", "-DUNIX_ENABLED", "-sSIDE_MODULE=1",
                                               "-sSUPPORT_LONGJMP=wasm"});
            if (options.web_thread_mode == NativeWebThreadMode::multi_threaded)
                arguments.emplace_back("-sUSE_PTHREADS=1");
        }
        if (options.profile == NativeBuildProfile::debug) {
            arguments.emplace_back("-O0");
            if (options.platform == NativePlatform::web) {
                arguments.emplace_back("-g2");
                arguments.emplace_back("-fdebug-compilation-dir=/gdpp/project");
            } else {
                arguments.emplace_back("-g");
            }
        } else {
            arguments.insert(arguments.end(), {"-O3", "-fvisibility=hidden", "-ffunction-sections",
                                               "-fdata-sections"});
        }
        arguments.push_back(options.profile == NativeBuildProfile::release ? "-DNDEBUG"
                                                                           : "-DDEBUG_ENABLED");
        append_reproducible_path_arguments(arguments, options);
        append_macos_architecture_arguments(arguments, options);
        append_android_target_arguments(arguments, options);
        append_include_arguments(arguments, includes, options.platform);
        arguments.emplace_back("-c");
        arguments.push_back(path_to_utf8(source));
        arguments.emplace_back("-o");
        arguments.push_back(path_to_utf8(object));
    }
    return command;
}

struct IOSBuildSlice {
    std::string name;
    std::string sdk;
    std::string target_triple;
    std::filesystem::path binding_library;
};

NativeBuildCommand ios_compile_command(const NativeBuildOptions& options,
                                       const IOSBuildSlice& slice,
                                       const std::filesystem::path& source,
                                       const std::filesystem::path& object,
                                       const std::vector<std::filesystem::path>& includes) {
    NativeBuildCommand command;
    command.executable = options.compiler_executable;
    command.working_directory = options.project_output_directory;
    auto& arguments = command.arguments;
    arguments = {
        "--sdk",         slice.sdk,       "clang++",         "-target",       slice.target_triple,
        "-std=c++17",    "-fPIC",         "-fno-exceptions", "-DGDEXTENSION", "-DTHREADS_ENABLED",
        "-DIOS_ENABLED", "-DUNIX_ENABLED"};
    if (options.profile == NativeBuildProfile::debug) {
        arguments.insert(arguments.end(), {"-O0", "-g", "-DDEBUG_ENABLED"});
    } else {
        arguments.insert(arguments.end(), {"-O3", "-DNDEBUG", "-fvisibility=hidden",
                                           "-ffunction-sections", "-fdata-sections"});
    }
    append_reproducible_path_arguments(arguments, options);
    append_include_arguments(arguments, includes, options.platform);
    arguments.emplace_back("-c");
    arguments.push_back(path_to_utf8(source));
    arguments.emplace_back("-o");
    arguments.push_back(path_to_utf8(object));
    return command;
}

NativeBuildCommand ios_link_command(const NativeBuildOptions& options, const IOSBuildSlice& slice,
                                    const std::vector<std::filesystem::path>& objects,
                                    const std::vector<std::filesystem::path>& libraries,
                                    const std::filesystem::path& output,
                                    const std::filesystem::path& export_map) {
    NativeBuildCommand command;
    command.executable = options.compiler_executable;
    command.working_directory = options.project_output_directory;
    command.stage = 1;
    auto& arguments = command.arguments;
    arguments = {"--sdk", slice.sdk, "clang++", "-target", slice.target_triple, "-dynamiclib"};
    for (const auto& object : objects)
        arguments.push_back(path_to_utf8(object));
    arguments.push_back(path_to_utf8(slice.binding_library));
    for (const auto& library : libraries)
        arguments.push_back(path_to_utf8(library));
    arguments.push_back("-Wl,-exported_symbols_list," + path_to_utf8(export_map));
    arguments.emplace_back("-Wl,-install_name,@rpath/libgdpp_project.dylib");
    if (options.profile == NativeBuildProfile::release) {
        arguments.emplace_back("-Wl,-dead_strip");
        arguments.emplace_back("-Wl,-x");
    }
    arguments.emplace_back("-o");
    arguments.push_back(path_to_utf8(output));
    return command;
}

bool append_utf16(std::u16string& output, std::string_view input) {
    for (std::size_t index = 0; index < input.size();) {
        const auto lead = static_cast<unsigned char>(input[index]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80U) {
            codepoint = lead;
            length = 1;
        } else if ((lead & 0xe0U) == 0xc0U) {
            codepoint = lead & 0x1fU;
            length = 2;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            codepoint = lead & 0x0fU;
            length = 3;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            codepoint = lead & 0x07U;
            length = 4;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + length > input.size())
            return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(input[index + offset]);
            if ((continuation & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
            return false;
        if (codepoint < 0x10000U) {
            output.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000U;
            output.push_back(static_cast<char16_t>(0xd800U + (codepoint >> 10U)));
            output.push_back(static_cast<char16_t>(0xdc00U + (codepoint & 0x3ffU)));
        }
        index += length;
    }
    return true;
}

bool write_msvc_response_file(const std::filesystem::path& path,
                              const std::vector<std::string>& arguments) {
    std::string content;
    for (const auto& argument : arguments) {
        content.push_back('"');
        for (const char character : argument) {
            if (character == '"')
                content.push_back('\\');
            content.push_back(character);
        }
        content += "\"\r\n";
    }
    std::u16string utf16;
    if (!append_utf16(utf16, content))
        return false;
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output)
        return false;
    output.put(static_cast<char>(0xff));
    output.put(static_cast<char>(0xfe));
    for (const char16_t code_unit : utf16) {
        output.put(static_cast<char>(code_unit & 0xffU));
        output.put(static_cast<char>((code_unit >> 8U) & 0xffU));
    }
    return output.good();
}

std::optional<NativeBuildCommand> link_command(const NativeBuildOptions& options,
                                               const std::vector<std::filesystem::path>& objects,
                                               const std::filesystem::path& binding_library,
                                               const std::vector<std::filesystem::path>& libraries,
                                               const std::filesystem::path& output,
                                               const std::filesystem::path& response_file,
                                               const std::filesystem::path& export_map) {
    NativeBuildCommand command;
    command.working_directory = options.project_output_directory;
    command.stage = 1;
    auto& arguments = command.arguments;
    if (options.platform == NativePlatform::windows) {
        // cl.exe does not decode UTF-16 response files reliably. Invoke the MSVC
        // linker directly so large projects and non-ASCII paths use its native
        // UTF-16 response-file support instead of the Windows command-line limit.
        std::filesystem::path linker = options.compiler_executable;
        linker.replace_filename("link.exe");
        command.executable = linker.u8string();
        const auto import_library = response_file.parent_path() / "gdpp_project.lib";
        std::vector<std::string> response_arguments{
            "/DLL", options.architecture == "arm64" ? "/MACHINE:ARM64" : "/MACHINE:X64"};
        for (const auto& object : objects)
            response_arguments.push_back(object.u8string());
        response_arguments.push_back(binding_library.u8string());
        for (const auto& library : libraries)
            response_arguments.push_back(library.u8string());
        response_arguments.push_back("/IMPLIB:" + import_library.u8string());
        response_arguments.push_back("/OUT:" + output.u8string());
        if (options.profile == NativeBuildProfile::release) {
            response_arguments.emplace_back("/OPT:REF");
            response_arguments.emplace_back("/OPT:ICF");
            response_arguments.emplace_back("/INCREMENTAL:NO");
        }
        if (!write_msvc_response_file(response_file, response_arguments))
            return std::nullopt;
        arguments = {"/NOLOGO", "@" + response_file.u8string()};
    } else if (options.platform == NativePlatform::web) {
        command.executable = options.compiler_executable;
        arguments = {"-shared", "-sSIDE_MODULE=1", "-sWASM_BIGINT", "-sSUPPORT_LONGJMP=wasm",
                     "-fvisibility=hidden"};
        append_reproducible_path_arguments(arguments, options);
        if (options.web_thread_mode == NativeWebThreadMode::multi_threaded)
            arguments.emplace_back("-sUSE_PTHREADS=1");
        for (const auto& object : objects)
            arguments.push_back(path_to_utf8(object));
        arguments.push_back(path_to_utf8(binding_library));
        for (const auto& library : libraries)
            arguments.push_back(path_to_utf8(library));
        if (options.profile == NativeBuildProfile::release) {
            arguments.insert(arguments.end(), {"-O3", "-Wl,--gc-sections", "-s"});
        } else {
            arguments.insert(arguments.end(), {"-O0", "-g2", "-sASSERTIONS=1",
                                               "-fdebug-compilation-dir=/gdpp/project"});
        }
        arguments.emplace_back("-o");
        arguments.push_back(path_to_utf8(output));
    } else {
        command.executable = options.compiler_executable;
        arguments.push_back(options.platform == NativePlatform::macos ? "-dynamiclib" : "-shared");
        append_macos_architecture_arguments(arguments, options);
        append_android_target_arguments(arguments, options);
        for (const auto& object : objects)
            arguments.push_back(path_to_utf8(object));
        arguments.push_back(path_to_utf8(binding_library));
        for (const auto& library : libraries)
            arguments.push_back(path_to_utf8(library));
        if (options.platform == NativePlatform::linux ||
            options.platform == NativePlatform::android) {
            // Keep the statically linked godot-cpp/C++ runtime private to each ELF GDExtension.
            // Multiple extensions are loaded into one process and ELF symbol interposition across
            // their standard-library locale/allocator state is undefined and has caused crashes.
            arguments.emplace_back("-Wl,--exclude-libs,ALL");
            arguments.push_back("-Wl,--version-script=" + path_to_utf8(export_map));
        } else if (options.platform == NativePlatform::macos) {
            // Mach-O exports symbols pulled from static archives unless an explicit allow-list
            // is supplied. Only the GDExtension ABI entry point belongs to the customer binary.
            arguments.push_back("-Wl,-exported_symbols_list," + path_to_utf8(export_map));
        }
        if (options.profile == NativeBuildProfile::release) {
            if (options.platform == NativePlatform::macos) {
                arguments.emplace_back("-Wl,-dead_strip");
                // Distribution images do not need the local symbol table. Keep the public
                // GDExtension entry point while reducing package size and exposed internals.
                arguments.emplace_back("-Wl,-x");
            } else {
                arguments.insert(arguments.end(), {"-Wl,--gc-sections", "-Wl,-O1", "-Wl,-s"});
                if (options.platform == NativePlatform::android) {
                    arguments.insert(arguments.end(), {"-Wl,-z,relro", "-Wl,-z,now"});
                }
            }
        }
        arguments.emplace_back("-o");
        arguments.push_back(path_to_utf8(output));
    }
    return command;
}

} // namespace

const char* native_build_profile_name(NativeBuildProfile profile) noexcept {
    switch (profile) {
    case NativeBuildProfile::development:
        return "development";
    case NativeBuildProfile::debug:
        return "debug";
    case NativeBuildProfile::release:
        return "release";
    }
    return "development";
}

std::optional<NativeBuildProfile> parse_native_build_profile(std::string_view value) noexcept {
    if (value == "development")
        return NativeBuildProfile::development;
    if (value == "debug")
        return NativeBuildProfile::debug;
    if (value == "release")
        return NativeBuildProfile::release;
    return std::nullopt;
}

std::string native_library_name(NativeBuildProfile profile, NativePlatform platform,
                                std::string_view architecture, std::string_view build_id,
                                NativeWebThreadMode web_thread_mode) {
    std::string stem = "gdpp_project." + std::string{native_build_profile_name(profile)} + "." +
                       platform_name(platform) + "." + std::string{architecture};
    if (profile == NativeBuildProfile::development)
        stem += "." + std::string{build_id};
    if (platform == NativePlatform::web)
        stem += "." + web_thread_mode_name(web_thread_mode);
    if (platform == NativePlatform::windows)
        return stem + ".dll";
    if (platform == NativePlatform::web)
        return "lib" + stem + ".wasm";
    if (platform == NativePlatform::ios)
        return "lib" + stem + ".xcframework";
    return "lib" + stem + (platform == NativePlatform::macos ? ".dylib" : ".so");
}

std::string native_development_extension_descriptor(GodotVersion target_version,
                                                    NativePlatform platform,
                                                    std::string_view architecture,
                                                    std::string_view resource_library_path,
                                                    NativeWebThreadMode web_thread_mode) {
    std::ostringstream output;
    output << "[configuration]\n\n"
           << "entry_symbol = \"gdpp_project_library_init\"\n"
           << "compatibility_minimum = \"" << godot_version_name(target_version) << "\"\n"
           << "reloadable = true\n\n"
           << "[libraries]\n\n";
    // "universal" describes the Mach-O payload, not a Godot runtime feature tag. A Universal 2
    // process still reports exactly one active CPU architecture, so both supported architecture
    // keys must resolve to the same fat library.
    if (platform == NativePlatform::macos && architecture == "universal") {
        output << "macos.editor.arm64 = \"" << resource_library_path << "\"\n"
               << "macos.editor.x86_64 = \"" << resource_library_path << "\"\n";
    } else if (platform == NativePlatform::web) {
        output << "web.editor.";
        if (web_thread_mode == NativeWebThreadMode::multi_threaded)
            output << "threads.";
        output << "wasm32 = \"" << resource_library_path << "\"\n";
    } else {
        output << platform_name(platform) << ".editor." << architecture << " = \""
               << resource_library_path << "\"\n";
    }
    return output.str();
}

NativeArtifactCleanupResult
prune_stale_development_libraries(const std::filesystem::path& current_library) {
    NativeArtifactCleanupResult result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(current_library, error) || error) {
        result.diagnostics.push_back("current development library does not exist: " +
                                     path_to_utf8(current_library));
        return result;
    }

    const auto filename = current_library.filename().string();
    const auto extension = current_library.extension().string();
    const auto stem = filename.substr(0, filename.size() - extension.size());
    const auto build_separator = stem.rfind('.');
    if (build_separator == std::string::npos) {
        result.diagnostics.push_back("development library has no content build identifier: " +
                                     filename);
        return result;
    }
    const auto build_id = stem.substr(build_separator + 1);
    if (build_id.size() != 16 ||
        !std::all_of(build_id.begin(), build_id.end(), [](const char character) {
            return std::isxdigit(static_cast<unsigned char>(character)) != 0;
        })) {
        result.diagnostics.push_back(
            "development library has an invalid content build identifier: " + filename);
        return result;
    }
    const auto family_prefix = stem.substr(0, build_separator + 1);
    const bool known_family = family_prefix.rfind("gdpp_project.development.", 0) == 0 ||
                              family_prefix.rfind("libgdpp_project.development.", 0) == 0;
    if (!known_family) {
        result.diagnostics.push_back("refusing to prune an unknown native artifact family: " +
                                     filename);
        return result;
    }

    for (std::filesystem::directory_iterator iterator{current_library.parent_path(), error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const auto candidate = iterator->path();
        if (candidate.filename() == current_library.filename() ||
            candidate.extension() != extension ||
            !std::filesystem::is_regular_file(iterator->symlink_status())) {
            continue;
        }
        const auto candidate_filename = candidate.filename().string();
        const auto candidate_stem = candidate_filename.substr(
            0, candidate_filename.size() - candidate.extension().string().size());
        if (candidate_stem.rfind(family_prefix, 0) != 0)
            continue;
        const auto candidate_build_id = candidate_stem.substr(family_prefix.size());
        if (candidate_build_id.size() != 16 ||
            !std::all_of(candidate_build_id.begin(), candidate_build_id.end(),
                         [](const char character) {
                             return std::isxdigit(static_cast<unsigned char>(character)) != 0;
                         })) {
            continue;
        }
        std::filesystem::remove(candidate, error);
        if (error) {
            result.diagnostics.push_back("cannot remove stale development library '" +
                                         path_to_utf8(candidate) + "': " + error.message());
            error.clear();
            continue;
        }
        ++result.removed_count;
    }
    if (error) {
        result.diagnostics.push_back("cannot inspect native artifact directory '" +
                                     path_to_utf8(current_library.parent_path()) +
                                     "': " + error.message());
    }
    result.success = result.diagnostics.empty();
    return result;
}

NativeBuildPlan NativeBuilder::plan(const NativeBuildOptions& options) const {
    NativeBuildPlan result;
    if (options.compiler_executable.empty()) {
        result.diagnostics.emplace_back("C++ compiler executable is not configured");
        return result;
    }
    if (options.binary_output_directory.empty()) {
        result.diagnostics.emplace_back(
            "project native library output directory is not configured");
        return result;
    }
    const bool architecture_supported =
        (options.platform == NativePlatform::macos &&
         (options.architecture == "arm64" || options.architecture == "x86_64" ||
          options.architecture == "universal")) ||
        (options.platform == NativePlatform::ios && options.architecture == "arm64") ||
        (options.platform == NativePlatform::web && options.architecture == "wasm32") ||
        (options.platform != NativePlatform::macos && options.platform != NativePlatform::ios &&
         options.platform != NativePlatform::web &&
         (options.architecture == "arm64" || options.architecture == "x86_64"));
    if (!architecture_supported) {
        result.diagnostics.push_back("unsupported native architecture '" + options.architecture +
                                     "' for " + platform_name(options.platform));
        return result;
    }
    if (options.platform == NativePlatform::web) {
        if (options.profile == NativeBuildProfile::development) {
            result.diagnostics.emplace_back(
                "Web distribution libraries support only debug or release profiles; "
                "editor development validation must use the host platform library");
            return result;
        }
        if (options.web_thread_mode == NativeWebThreadMode::not_applicable) {
            result.diagnostics.emplace_back(
                "Web target must explicitly select threads or nothreads");
            return result;
        }
    } else if (options.platform == NativePlatform::ios) {
        if (options.profile == NativeBuildProfile::development) {
            result.diagnostics.emplace_back(
                "iOS distribution libraries support only debug or release profiles; "
                "editor development validation must use the macOS host library");
            return result;
        }
    } else if (options.web_thread_mode != NativeWebThreadMode::not_applicable) {
        result.diagnostics.emplace_back("Web thread mode is invalid for a non-Web target");
        return result;
    }
    if (!validate_manifest(options, result.diagnostics))
        return result;
    const auto build_id = read_build_id(options.project_output_directory);
    if (build_id.empty()) {
        result.diagnostics.emplace_back("missing or invalid project native build identifier");
        return result;
    }
    const auto generated = options.project_output_directory / "generated";
    const auto registration = options.project_output_directory / "register_types.cpp";
    const auto runtime = options.sdk_root / "src/runtime/variant_ops.cpp";
    std::vector<std::filesystem::path> includes{
        generated,
        options.sdk_root / "include",
        options.sdk_root / "godot-cpp/include",
        options.sdk_root / "godot-cpp/gen/include",
    };
    const auto bridge_inputs = read_bridge_lock(options.project_output_directory / "bridge.lock",
                                                options, result.diagnostics);
    if (!bridge_inputs)
        return result;
    includes.insert(includes.end(), bridge_inputs->include_directories.begin(),
                    bridge_inputs->include_directories.end());
    for (const auto& include : bridge_inputs->include_directories) {
        if (!std::filesystem::is_directory(include))
            result.diagnostics.push_back("missing third-party bridge include directory: " +
                                         path_to_utf8(include));
    }
    for (const auto& library : bridge_inputs->link_libraries) {
        if (!std::filesystem::is_regular_file(library))
            result.diagnostics.push_back("missing third-party bridge link library: " +
                                         path_to_utf8(library));
    }
    for (const auto& required :
         {registration, runtime, includes[1] / "gdpp/runtime/variant_ops.hpp",
          includes[2] / "godot_cpp/godot.hpp", includes[3] / "godot_cpp/core/version.hpp",
          includes[3] / "gdextension_interface.h"}) {
        if (!std::filesystem::is_regular_file(required))
            result.diagnostics.push_back("missing native SDK input: " + path_to_utf8(required));
    }
    // Godot-cpp uses upstream ABI target names internally. They are deliberately kept out of
    // GDPP's public build-profile API and artifact names.
    const std::string binding_target = options.profile == NativeBuildProfile::development ? "editor"
                                       : options.profile == NativeBuildProfile::debug
                                           ? "template_debug"
                                           : "template_release";
    std::filesystem::path binding_library;
    std::filesystem::path ios_device_binding_library;
    std::filesystem::path ios_simulator_binding_library;
    if (options.platform == NativePlatform::ios) {
        ios_device_binding_library =
            find_binding_library(options.sdk_root / "lib/device", options, binding_target);
        ios_simulator_binding_library =
            find_binding_library(options.sdk_root / "lib/simulator", options, binding_target);
        if (ios_device_binding_library.empty() || ios_simulator_binding_library.empty()) {
            result.diagnostics.emplace_back(
                "missing device or Universal Simulator godot-cpp library in iOS SDK");
        }
    } else {
        binding_library = find_binding_library(options.sdk_root / "lib", options, binding_target);
        if (binding_library.empty())
            result.diagnostics.emplace_back(
                "missing ABI-compatible godot-cpp static library in SDK");
    }
    if (!result.diagnostics.empty())
        return result;

    std::vector<std::filesystem::path> sources{registration, runtime};
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{generated, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() &&
            ends_with(iterator->path().filename().string(), ".gd.cpp"))
            sources.push_back(iterator->path());
    }
    if (error || sources.size() == 2) {
        result.diagnostics.emplace_back("no generated project translation units were found");
        return result;
    }
    std::sort(sources.begin(), sources.end());

    auto native = options.project_output_directory / "native-direct" /
                  godot_version_name(options.target_version) / platform_name(options.platform) /
                  options.architecture;
    if (options.platform == NativePlatform::web)
        native /= web_thread_mode_name(options.web_thread_mode);
    native /= native_build_profile_name(options.profile);
    const auto objects_directory = native / "objects";
    const auto build_configuration = native / "build-configuration.txt";
    const auto export_map = native / "gdpp.exports.map";
    const auto& binary_directory = options.binary_output_directory;
    std::filesystem::create_directories(objects_directory, error);
    std::filesystem::create_directories(binary_directory, error);
    if (error) {
        result.diagnostics.emplace_back("cannot create native build directories: " +
                                        error.message());
        return result;
    }
    std::string build_configuration_contents =
        "GDPP_NATIVE_BUILD " + std::string{native_build_revision} + "\napi " +
        std::string{godot_version_name(options.target_version)} + "\nplatform " +
        platform_name(options.platform) + "\narch " + options.architecture + "\nprofile " +
        native_build_profile_name(options.profile) + "\ncompiler " + options.compiler_executable +
        "\nweb_threads " + web_thread_mode_name(options.web_thread_mode) + "\n";
    for (const auto& [source, replacement] : reproducible_path_mappings(options))
        build_configuration_contents += "path_map " + source + "=" + replacement + "\n";
    const auto build_configuration_changed =
        write_file_if_changed(build_configuration, build_configuration_contents);
    if (!build_configuration_changed) {
        result.diagnostics.emplace_back("cannot write native build configuration signature");
        return result;
    }
    if (options.platform == NativePlatform::linux || options.platform == NativePlatform::android) {
        if (!write_file_if_changed(export_map,
                                   "{ global: gdpp_project_library_init; local: *; };\n")) {
            result.diagnostics.emplace_back("cannot write ELF project export map");
            return result;
        }
    } else if (options.platform == NativePlatform::macos ||
               options.platform == NativePlatform::ios) {
        if (!write_file_if_changed(export_map, "_gdpp_project_library_init\n")) {
            result.diagnostics.emplace_back("cannot write Mach-O project export list");
            return result;
        }
    }
    result.output_library =
        binary_directory / native_library_name(options.profile, options.platform,
                                               options.architecture, build_id,
                                               options.web_thread_mode);

    std::vector<std::filesystem::path> compile_inputs{
        build_configuration,
        options.sdk_root / "sdk.manifest",
        options.sdk_root / "include/gdpp/runtime/variant_ops.hpp",
    };
    const auto bridge_lock = options.project_output_directory / "bridge.lock";
    if (std::filesystem::is_regular_file(bridge_lock))
        compile_inputs.push_back(bridge_lock);
    compile_inputs.insert(compile_inputs.end(), bridge_inputs->manifests.begin(),
                          bridge_inputs->manifests.end());
    for (const auto& include : bridge_inputs->include_directories) {
        for (std::filesystem::recursive_directory_iterator iterator{include, error}, end;
             !error && iterator != end; iterator.increment(error)) {
            if (iterator->is_regular_file())
                compile_inputs.push_back(iterator->path());
        }
        if (error) {
            result.diagnostics.push_back("cannot inspect third-party bridge headers in '" +
                                         path_to_utf8(include) + "': " + error.message());
            return result;
        }
    }
    std::sort(compile_inputs.begin(), compile_inputs.end());
    compile_inputs.erase(std::unique(compile_inputs.begin(), compile_inputs.end()),
                         compile_inputs.end());
    for (std::filesystem::directory_iterator iterator{generated, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() && iterator->path().extension() == ".hpp")
            compile_inputs.push_back(iterator->path());
    }

    if (options.platform == NativePlatform::ios) {
        const auto deployment_target =
            manifest_value(options.sdk_root / "sdk.manifest", "ios_deployment_target");
        if (deployment_target.empty()) {
            result.diagnostics.emplace_back("native iOS SDK deployment target is unavailable");
            return result;
        }
        const std::vector<IOSBuildSlice> slices{
            {"device-arm64", "iphoneos", "arm64-apple-ios" + deployment_target,
             ios_device_binding_library},
            {"simulator-arm64", "iphonesimulator",
             "arm64-apple-ios" + deployment_target + "-simulator", ios_simulator_binding_library},
            {"simulator-x86_64", "iphonesimulator",
             "x86_64-apple-ios" + deployment_target + "-simulator", ios_simulator_binding_library},
        };

        result.output_library =
            binary_directory / native_library_name(options.profile, options.platform,
                                                   options.architecture, build_id,
                                                   options.web_thread_mode);
        const auto staging_directory = native / "xcframework-staging";
        result.pending_output_library = staging_directory / result.output_library.filename();

        std::vector<std::filesystem::path> slice_libraries;
        std::vector<bool> slice_relinked;
        for (const auto& slice : slices) {
            const auto slice_directory = native / slice.name;
            const auto slice_objects = slice_directory / "objects";
            std::filesystem::create_directories(slice_objects, error);
            if (error) {
                result.diagnostics.push_back("cannot create iOS slice build directory: " +
                                             error.message());
                return result;
            }
            std::vector<std::filesystem::path> objects;
            bool compiled = false;
            for (const auto& source : sources) {
                const auto object = slice_objects / (safe_stem(source) + ".o");
                objects.push_back(object);
                auto inputs = compile_inputs;
                inputs.push_back(source);
                if (*build_configuration_changed || older_than(object, inputs)) {
                    result.commands.push_back(
                        ios_compile_command(options, slice, source, object, includes));
                    compiled = true;
                }
            }
            const auto slice_library = slice_directory / "libgdpp_project.dylib";
            slice_libraries.push_back(slice_library);
            auto link_inputs = objects;
            link_inputs.push_back(slice.binding_library);
            link_inputs.push_back(export_map);
            link_inputs.insert(link_inputs.end(), bridge_inputs->link_libraries.begin(),
                               bridge_inputs->link_libraries.end());
            const bool relink = compiled || older_than(slice_library, link_inputs);
            slice_relinked.push_back(relink);
            if (relink) {
                result.commands.push_back(ios_link_command(options, slice, objects,
                                                           bridge_inputs->link_libraries,
                                                           slice_library, export_map));
            }
        }

        const auto simulator_directory = native / "simulator-universal";
        std::filesystem::create_directories(simulator_directory, error);
        if (error) {
            result.diagnostics.push_back("cannot create iOS Simulator build directory: " +
                                         error.message());
            return result;
        }
        const auto simulator_library = simulator_directory / "libgdpp_project.dylib";
        const bool relipo = slice_relinked[1] || slice_relinked[2] ||
                            older_than(simulator_library, {slice_libraries[1], slice_libraries[2]});
        if (relipo) {
            NativeBuildCommand command;
            command.executable = options.compiler_executable;
            command.working_directory = options.project_output_directory;
            command.stage = 2;
            command.arguments = {"lipo",
                                 "-create",
                                 path_to_utf8(slice_libraries[1]),
                                 path_to_utf8(slice_libraries[2]),
                                 "-output",
                                 path_to_utf8(simulator_library)};
            result.commands.push_back(std::move(command));
        }

        const bool repackage = slice_relinked[0] || relipo ||
                               older_than(result.output_library / "Info.plist",
                                          {slice_libraries[0], simulator_library});
        if (repackage) {
            std::filesystem::remove_all(result.pending_output_library, error);
            if (error) {
                result.diagnostics.push_back("cannot clear stale iOS XCFramework staging output: " +
                                             error.message());
                return result;
            }
            std::filesystem::create_directories(staging_directory, error);
            if (error) {
                result.diagnostics.push_back("cannot create iOS XCFramework staging directory: " +
                                             error.message());
                return result;
            }
            NativeBuildCommand command;
            command.executable = options.compiler_executable;
            command.working_directory = options.project_output_directory;
            command.stage = 3;
            command.arguments = {"xcodebuild", "-create-xcframework",
                                 "-library",   path_to_utf8(slice_libraries[0]),
                                 "-library",   path_to_utf8(simulator_library),
                                 "-output",    path_to_utf8(result.pending_output_library)};
            result.commands.push_back(std::move(command));
        } else {
            result.pending_output_library.clear();
        }
        result.up_to_date = result.commands.empty();
        result.success = true;
        return result;
    }

    std::vector<std::filesystem::path> objects;
    bool compiled = false;
    for (const auto& source : sources) {
        const auto object =
            objects_directory / (safe_stem(source) + object_extension(options.platform));
        objects.push_back(object);
        auto inputs = compile_inputs;
        inputs.push_back(source);
        if (*build_configuration_changed || older_than(object, inputs)) {
            result.commands.push_back(compile_command(options, source, object, includes));
            compiled = true;
        }
    }
    auto link_inputs = objects;
    link_inputs.push_back(binding_library);
    if (std::filesystem::is_regular_file(export_map))
        link_inputs.push_back(export_map);
    link_inputs.insert(link_inputs.end(), bridge_inputs->link_libraries.begin(),
                       bridge_inputs->link_libraries.end());
    if (compiled || older_than(result.output_library, link_inputs)) {
        auto command =
            link_command(options, objects, binding_library, bridge_inputs->link_libraries,
                         result.output_library, native / "link.rsp", export_map);
        if (!command) {
            result.diagnostics.emplace_back("cannot write MSVC native linker response file");
            return result;
        }
        result.commands.push_back(std::move(*command));
    }
    result.up_to_date = result.commands.empty();
    result.success = true;
    return result;
}

} // namespace gdpp
