extends SceneTree

func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    var compiler := GDPPCompiler.new()
    var sdk_root := ProjectSettings.globalize_path("res://addons/gdpp/sdk")
    var fixture_root := "res://addons/gdpp/build/version-4.7-fixture"
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root))
    var source := FileAccess.open(fixture_root + "/versioned.gd", FileAccess.WRITE)
    if source == null:
        push_error("GDPP could not create its 4.7 integration fixture")
        quit(1)
        return
    source.store_string(
        "extends Node\n"
        + "class_name VersionedNativeProject\n"
        + "func hdr_enabled() -> bool:\n"
        + "    return DisplayServer.window_is_hdr_output_enabled()\n"
    )
    source.close()
    var result: Dictionary = compiler.compile_project(
        fixture_root,
        fixture_root + "/native",
        sdk_root,
        compiler.get_default_compiler_executable(),
        "4.7"
    )
    if not result.get("success", false):
        push_error("GDPP 4.7 build planning failed: %s" % result.get("diagnostics", []))
        quit(1)
        return
    var execution: Dictionary = compiler.execute_project_build(result)
    if not execution.get("success", false):
        push_error("GDPP 4.7 compiler failed: %s" % execution.get("diagnostics", []))
        quit(1)
        return
    var descriptor := FileAccess.get_file_as_string(
        fixture_root + "/native/gdpp_project.gdextension"
    )
    if 'compatibility_minimum = "4.7"' not in descriptor:
        push_error("GDPP emitted an invalid 4.7 compatibility descriptor")
        quit(1)
        return
    if not FileAccess.file_exists(str(result.get("output_library", ""))):
        push_error("GDPP 4.7 direct build did not produce its native library")
        quit(1)
        return
    var binary_directory := fixture_root + "/addons/gdpp/binary"
    var current_name := str(result.get("output_library", "")).get_file()
    var current_extension := current_name.get_extension()
    var build_id := str(result.get("build_id", ""))
    var family_prefix := current_name.trim_suffix(build_id + "." + current_extension)
    var development_count := 0
    for file_name in DirAccess.get_files_at(binary_directory):
        if file_name.begins_with(family_prefix) and file_name.get_extension() == current_extension:
            development_count += 1
    if development_count != 1:
        push_error("GDPP 4.7 build retained %d development libraries" % development_count)
        quit(1)
        return
    print("GDPP_DIRECT_BUILD_4_7_OK")
    quit(0)
