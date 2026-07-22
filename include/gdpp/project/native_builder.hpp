#pragma once

#include "gdpp/core/godot_version.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdpp {

enum class NativePlatform { macos, linux, windows, android, ios, web };
enum class NativeBuildProfile { development, debug, release };
enum class NativeWebThreadMode { not_applicable, single_threaded, multi_threaded };

[[nodiscard]] const char* native_build_profile_name(NativeBuildProfile profile) noexcept;
[[nodiscard]] std::optional<NativeBuildProfile>
parse_native_build_profile(std::string_view value) noexcept;
[[nodiscard]] bool native_architecture_supported(NativePlatform platform,
                                                 std::string_view architecture) noexcept;
[[nodiscard]] std::string
native_library_name(NativeBuildProfile profile, NativePlatform platform,
                    std::string_view architecture, std::string_view build_id,
                    NativeWebThreadMode web_thread_mode = NativeWebThreadMode::not_applicable);
[[nodiscard]] std::string native_development_extension_descriptor(
    GodotVersion target_version, NativePlatform platform, std::string_view architecture,
    std::string_view resource_library_path,
    NativeWebThreadMode web_thread_mode = NativeWebThreadMode::not_applicable,
    std::string_view additional_sections = {}, bool reloadable = true);

struct NativeBuildOptions {
    std::filesystem::path project_output_directory;
    std::filesystem::path binary_output_directory;
    std::filesystem::path sdk_root;
    std::string compiler_executable;
    NativePlatform platform{NativePlatform::linux};
    std::string architecture{"x86_64"};
    NativeBuildProfile profile{NativeBuildProfile::development};
    NativeWebThreadMode web_thread_mode{NativeWebThreadMode::not_applicable};
    GodotVersion target_version{minimum_godot_version};
};

struct NativeBuildCommand {
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    // Commands in one stage are independent and may execute in parallel. A later stage starts
    // only after every command in the preceding stage succeeds.
    std::size_t stage{0};
};

struct NativeBuildPlan {
    bool success{false};
    bool up_to_date{false};
    std::vector<NativeBuildCommand> commands;
    std::vector<std::string> diagnostics;
    std::filesystem::path output_library;
    // Directory artifacts such as an Apple XCFramework are assembled away from the customer
    // output and committed only after the complete build succeeds.
    std::filesystem::path pending_output_library;
};

struct NativeArtifactCleanupResult {
    bool success{false};
    std::size_t removed_count{0};
    std::vector<std::string> diagnostics;
};

// Development libraries carry a content build ID so Godot can load a repaired image without
// reusing an already mapped binary. Prune only older images for the same profile/platform/CPU;
// libraries for other export targets are independent customer artifacts and must be retained.
[[nodiscard]] NativeArtifactCleanupResult
prune_stale_development_libraries(const std::filesystem::path& current_library);

class NativeBuilder final {
  public:
    [[nodiscard]] NativeBuildPlan plan(const NativeBuildOptions& options) const;
};

} // namespace gdpp
