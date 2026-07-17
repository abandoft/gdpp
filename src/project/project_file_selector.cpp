#include "project_file_selector.hpp"

#include <iterator>
#include <string_view>

namespace gdpp::project {
namespace {

bool starts_with(const std::filesystem::path& path, const std::filesystem::path& prefix) noexcept {
    auto path_part = path.begin();
    auto prefix_part = prefix.begin();
    while (prefix_part != prefix.end()) {
        if (path_part == path.end() || *path_part != *prefix_part)
            return false;
        ++path_part;
        ++prefix_part;
    }
    return prefix_part == prefix.end();
}

bool is_platform_metadata(const std::filesystem::path& path) noexcept {
    for (const auto& component : path) {
        const auto name = component.filename().string();
        // Finder resource forks are regular files whose apparent suffix can still be .gd,
        // .tscn or .gdextension.  __MACOSX is the equivalent container produced by ZIP tools.
        // Neither is project content and both are commonly introduced when a customer project
        // is copied from macOS to a case-sensitive build host.
        if (name.rfind("._", 0) == 0 || name == "__MACOSX" || name == ".DS_Store")
            return true;
    }
    return false;
}

} // namespace

ProjectFileSelector::ProjectFileSelector(std::filesystem::path project_root,
                                         std::filesystem::path compiler_output)
    : output_relative_(std::move(compiler_output)
                           .lexically_relative(std::move(project_root))
                           .lexically_normal()) {}

PathDisposition
ProjectFileSelector::classify(const std::filesystem::path& project_relative_path) const noexcept {
    const auto normalized = project_relative_path.lexically_normal();
    if (is_platform_metadata(normalized))
        return PathDisposition::platform_metadata;
    const auto first = normalized.empty() ? std::filesystem::path{} : *normalized.begin();
    if (first == ".git")
        return PathDisposition::version_control;
    if (first == ".godot")
        return PathDisposition::engine_cache;
    // Root build/ is the conventional delivery/test output.  A game is still allowed to use a
    // nested directory named build (for example res://systems/build/).
    if (first == "build")
        return PathDisposition::delivery_output;
    if (!output_relative_.empty() && starts_with(normalized, output_relative_))
        return PathDisposition::compiler_output;
    const std::filesystem::path gdpp_root{"addons/gdpp"};
    if (starts_with(normalized, gdpp_root))
        return PathDisposition::gdpp_plugin;
    return PathDisposition::project_content;
}

bool ProjectFileSelector::should_descend(
    const std::filesystem::path& project_relative_path) const noexcept {
    return classify(project_relative_path) == PathDisposition::project_content;
}

bool ProjectFileSelector::should_compile(
    const std::filesystem::path& project_relative_path) const noexcept {
    return classify(project_relative_path) == PathDisposition::project_content;
}

} // namespace gdpp::project
