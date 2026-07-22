#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace gdpp::extension {

class GDPPCompiler final : public godot::RefCounted {
    GDCLASS(GDPPCompiler, godot::RefCounted)

  public:
    [[nodiscard]] godot::Dictionary compile_source(const godot::String& source,
                                                   const godot::String& virtual_path,
                                                   const godot::String& target_version) const;
    [[nodiscard]] godot::Dictionary compile_file(const godot::String& source_path,
                                                 const godot::String& output_directory,
                                                 const godot::String& target_version) const;
    [[nodiscard]] godot::Dictionary
    compile_project(const godot::String& project_root, const godot::String& output_directory,
                    const godot::String& sdk_root, const godot::String& compiler_executable,
                    const godot::String& target_version, const godot::String& build_profile,
                    const godot::String& target_platform, const godot::String& target_architecture,
                    const godot::String& target_variant) const;
    [[nodiscard]] godot::Dictionary
    execute_project_build(const godot::Dictionary& build_plan,
                          const godot::Callable& progress_callback = {}) const;
    [[nodiscard]] godot::Dictionary
    prune_stale_development_libraries(const godot::String& current_library) const;
    [[nodiscard]] godot::String get_default_sdk_root() const;
    [[nodiscard]] godot::String get_default_compiler_executable() const;
    [[nodiscard]] godot::String get_host_platform() const;
    [[nodiscard]] godot::String get_host_architecture() const;
    [[nodiscard]] bool is_target_supported(const godot::String& platform,
                                           const godot::String& architecture) const;
    [[nodiscard]] godot::PackedStringArray get_supported_godot_versions() const;

  protected:
    static void _bind_methods();

  private:
    [[nodiscard]] int64_t execute_build_commands(const godot::Array& commands,
                                                 const godot::Callable& progress_callback) const;
};

} // namespace gdpp::extension
