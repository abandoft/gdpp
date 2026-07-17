#pragma once

#include <optional>
#include <string_view>

namespace gdpp {

enum class GodotVersion { v4_4, v4_5, v4_6, v4_7 };

inline constexpr GodotVersion minimum_godot_version = GodotVersion::v4_4;
inline constexpr GodotVersion latest_godot_version = GodotVersion::v4_7;

[[nodiscard]] std::string_view godot_version_name(GodotVersion version) noexcept;
[[nodiscard]] std::optional<GodotVersion> parse_godot_version(std::string_view version) noexcept;
[[nodiscard]] bool supports_godot_version(int major, int minor) noexcept;
[[nodiscard]] std::optional<GodotVersion> best_godot_version(int major, int minor) noexcept;

} // namespace gdpp
