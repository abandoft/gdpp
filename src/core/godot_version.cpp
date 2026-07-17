#include "gdpp/core/godot_version.hpp"

namespace gdpp {

std::string_view godot_version_name(GodotVersion version) noexcept {
    switch (version) {
    case GodotVersion::v4_4:
        return "4.4";
    case GodotVersion::v4_5:
        return "4.5";
    case GodotVersion::v4_6:
        return "4.6";
    case GodotVersion::v4_7:
        return "4.7";
    }
    return "4.4";
}

std::optional<GodotVersion> parse_godot_version(std::string_view version) noexcept {
    if (version == "4.4" || version.rfind("4.4.", 0) == 0)
        return GodotVersion::v4_4;
    if (version == "4.5" || version.rfind("4.5.", 0) == 0)
        return GodotVersion::v4_5;
    if (version == "4.6" || version.rfind("4.6.", 0) == 0)
        return GodotVersion::v4_6;
    if (version == "4.7" || version.rfind("4.7.", 0) == 0)
        return GodotVersion::v4_7;
    return std::nullopt;
}

bool supports_godot_version(int major, int minor) noexcept {
    return major == 4 && minor >= 4 && minor <= 7;
}

std::optional<GodotVersion> best_godot_version(int major, int minor) noexcept {
    if (!supports_godot_version(major, minor))
        return std::nullopt;
    if (minor == 4)
        return GodotVersion::v4_4;
    if (minor == 5)
        return GodotVersion::v4_5;
    if (minor == 6)
        return GodotVersion::v4_6;
    if (minor == 7)
        return GodotVersion::v4_7;
    return std::nullopt;
}

} // namespace gdpp
