extends SceneTree

var _project_progress_samples: Array[Dictionary] = []
var _native_progress_samples: Array[Dictionary] = []


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
        target_version,
        "development",
        "",
        "",
        "",
        Callable(self, "_record_project_progress")
    )
    print("GDPP_DIRECT_BUILD_PLAN_END commands=", result.get("build_commands", []).size())
    if not result.get("success", false):
        push_error("GDPP direct build planning failed: %s" % result.get("diagnostics", []))
        quit(1)
        return
    if not _validate_project_progress():
        quit(1)
        return
    var command_count := int(result.get("build_commands", []).size())
    var execution: Dictionary = compiler.execute_project_build(
        result,
        Callable(self, "_record_native_progress")
    )
    var exit_code := int(execution.get("exit_code", -1))
    print("GDPP_DIRECT_BUILD_EXIT code=", exit_code)
    if not execution.get("success", false):
        push_error("GDPP direct compiler failed: %s" % execution.get("diagnostics", []))
        quit(1)
        return
    if command_count > 0 and not _validate_build_progress(command_count):
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
    var abstract_scripts: PackedStringArray = result.get("abstract_scripts", PackedStringArray())
    var hello_class := str(classes.get("res://hello.gd", ""))
    if (
        "[icons]" not in descriptor
        or '%s = "res://icon.svg"' % hello_class not in descriptor
    ):
        push_error("GDPP development descriptor lost @icon metadata")
        quit(1)
        return
    if abstract_scripts != PackedStringArray(["res://abstract_contract.gd"]):
        push_error("GDPP compiler service lost abstract script metadata: %s" % abstract_scripts)
        quit(1)
        return
    print("GDPP_DIRECT_BUILD_OK")
    quit(0)


func _record_project_progress(phase: String, completed: int, total: int) -> void:
    _project_progress_samples.push_back({
        "phase": phase,
        "completed": completed,
        "total": total,
    })


func _record_native_progress(phase: String, completed: int, total: int) -> void:
    _native_progress_samples.push_back({
        "phase": phase,
        "completed": completed,
        "total": total,
    })


func _validate_project_progress() -> bool:
    return _validate_ordered_progress(
        _project_progress_samples,
        PackedStringArray(["scan", "parse", "analyze", "translate", "generate"]),
        "project"
    )


func _validate_build_progress(command_count: int) -> bool:
    if _native_progress_samples.is_empty():
        push_error("GDPP direct build emitted no progress samples")
        return false
    if not _validate_ordered_progress(
        _native_progress_samples,
        PackedStringArray(["compile", "link", "complete"]),
        "native"
    ):
        return false
    var final_sample: Dictionary = _native_progress_samples.back()
    if (
        final_sample.get("phase", "") != "complete"
        or int(final_sample.get("completed", -1)) != command_count
        or int(final_sample.get("total", -1)) != command_count
    ):
        push_error("GDPP direct build progress did not complete: %s" % final_sample)
        return false
    return true


func _validate_ordered_progress(
    samples: Array[Dictionary], expected_phases: PackedStringArray, label: String
) -> bool:
    if samples.is_empty():
        push_error("GDPP %s pipeline emitted no progress samples" % label)
        return false
    var phase_index := -1
    var previous_by_phase: Dictionary = {}
    var completed_phases: Dictionary = {}
    for sample: Dictionary in samples:
        var phase := str(sample.get("phase", ""))
        var current_phase_index := expected_phases.find(phase)
        if current_phase_index < phase_index or current_phase_index < 0:
            push_error("GDPP %s pipeline emitted phases out of order: %s" % [label, sample])
            return false
        phase_index = current_phase_index
        var completed := int(sample.get("completed", -1))
        var total := int(sample.get("total", -1))
        var previous := int(previous_by_phase.get(phase, -1))
        if total <= 0 or completed < previous or completed > total:
            push_error("GDPP %s pipeline emitted invalid progress: %s" % [label, sample])
            return false
        previous_by_phase[phase] = completed
        if completed == total:
            completed_phases[phase] = true
    for phase in expected_phases:
        if not completed_phases.has(phase):
            push_error("GDPP %s pipeline did not complete phase '%s'" % [label, phase])
            return false
    return true
