extends SceneTree

func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    var compiler := GDPPCompiler.new()
    var sdk_root := ProjectSettings.globalize_path("res://addons/gdpp/sdk")
    var engine := Engine.get_version_info()
    var target_version := "%d.%d" % [int(engine.major), int(engine.minor)]
    print("GDPP_DIRECT_BUILD_PLAN_BEGIN")
    var result: Dictionary = compiler.compile_project(
        "res://",
        "res://addons/gdpp/build/project",
        sdk_root,
        compiler.get_default_compiler_executable(),
        target_version
    )
    print("GDPP_DIRECT_BUILD_PLAN_END commands=", result.get("build_commands", []).size())
    if not result.get("success", false):
        push_error("GDPP direct build planning failed: %s" % result.get("diagnostics", []))
        quit(1)
        return
    var execution: Dictionary = compiler.execute_project_build(result)
    var exit_code := int(execution.get("exit_code", -1))
    print("GDPP_DIRECT_BUILD_EXIT code=", exit_code)
    if not execution.get("success", false):
        push_error("GDPP direct compiler failed: %s" % execution.get("diagnostics", []))
        quit(1)
        return
    var library := str(result.get("output_library", ""))
    if not FileAccess.file_exists(library):
        push_error("GDPP direct build did not produce its native library")
        quit(1)
        return
    var current_name := library.get_file()
    var current_extension := current_name.get_extension()
    var build_id := str(result.get("build_id", ""))
    var family_prefix := current_name.trim_suffix(build_id + "." + current_extension)
    var development_count := 0
    for file_name in DirAccess.get_files_at("res://addons/gdpp/binary"):
        if file_name.begins_with(family_prefix) and file_name.get_extension() == current_extension:
            development_count += 1
    if development_count != 1:
        push_error("GDPP direct build retained %d development libraries" % development_count)
        quit(1)
        return
    var descriptor := FileAccess.get_file_as_string(str(result.get("extension_descriptor", "")))
    var classes: Dictionary = result.get("script_classes", {})
    var hello_class := str(classes.get("res://hello.gd", ""))
    if (
        "[icons]" not in descriptor
        or '%s = "res://icon.svg"' % hello_class not in descriptor
    ):
        push_error("GDPP development descriptor lost @icon metadata")
        quit(1)
        return
    print("GDPP_DIRECT_BUILD_OK")
    quit(0)
