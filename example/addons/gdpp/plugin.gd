@tool
extends EditorPlugin

const NATIVE_BUILD_JOB := preload("res://addons/gdpp/native_build_job.gd")
const OUTPUT_DIRECTORY := "res://addons/gdpp/build/project"
const COMPILER_SETTING := "gdpp/build/cpp_compiler"
const SDK_SETTING := "gdpp/build/sdk_root"
const ANDROID_NDK_SETTING := "gdpp/build/android_ndk_root"
const EMSCRIPTEN_CXX_SETTING := "gdpp/build/emscripten_cxx"
const TARGET_VERSION_SETTING := "gdpp/target_godot_version"
const COMPILER_DESCRIPTOR := "res://addons/gdpp/gdpp.gdextension"
const EXTENSION_REGISTRY := "res://.godot/extension_list.cfg"
const EXTENSION_REGISTRY_BACKUP := "res://.godot/gdpp_extension_list.export-backup"
const COMPILER_DESCRIPTOR_BACKUP := "res://.godot/gdpp_compiler_descriptor.export-backup"

var _compiler: Object
var _export_plugin: EditorExportPlugin
var _build_progress: CanvasLayer
var _building := false


func _enter_tree() -> void:
    _recover_interrupted_export()
    if not ClassDB.class_exists(&"GDPPCompiler"):
        push_error("GDPP: compiler extension is not loaded")
        return
    _compiler = ClassDB.instantiate(&"GDPPCompiler")
    if _compiler == null:
        push_error("GDPP: cannot instantiate compiler service")
        return
    var detected_version := _detected_target_version()
    if not _compiler.get_supported_godot_versions().has(detected_version):
        push_error(
            "GDPP: Godot %s is not certified by this plugin build; supported versions: %s" % [
                detected_version,
                ", ".join(_compiler.get_supported_godot_versions()),
            ]
        )
        _compiler = null
        return
    _define_setting(COMPILER_SETTING, _compiler.get_default_compiler_executable())
    _define_setting(SDK_SETTING, _default_sdk_root())
    _define_setting(ANDROID_NDK_SETTING, _default_android_ndk_root())
    _define_setting(EMSCRIPTEN_CXX_SETTING, "em++")
    _define_setting(TARGET_VERSION_SETTING, detected_version)
    ProjectSettings.add_property_info({
        "name": TARGET_VERSION_SETTING,
        "type": TYPE_STRING,
        "hint": PROPERTY_HINT_ENUM,
        "hint_string": ",".join(_compiler.get_supported_godot_versions()),
    })
    _build_progress = preload("res://addons/gdpp/build_progress.gd").new()
    EditorInterface.get_base_control().add_child(_build_progress)
    _export_plugin = preload("res://addons/gdpp/export_plugin.gd").new()
    _export_plugin.configure(_compiler, _build_progress)
    add_export_plugin(_export_plugin)
    add_tool_menu_item("Build GDPP Native Library", _compile_project)


func _recover_interrupted_export() -> void:
    var recovered := false
    recovered = _restore_export_file(COMPILER_DESCRIPTOR, COMPILER_DESCRIPTOR_BACKUP) or recovered
    recovered = _restore_export_file(EXTENSION_REGISTRY, EXTENSION_REGISTRY_BACKUP) or recovered
    if not recovered:
        return
    if not ClassDB.class_exists(&"GDPPCompiler"):
        GDExtensionManager.load_extension(COMPILER_DESCRIPTOR)


func _restore_export_file(path: String, backup_path: String) -> bool:
    if not FileAccess.file_exists(backup_path):
        return false
    var backup := FileAccess.get_file_as_string(backup_path)
    var target := FileAccess.open(path, FileAccess.WRITE)
    if target == null:
        push_error("GDPP: cannot recover '%s' after an interrupted export" % path)
        return false
    target.store_string(backup)
    if target.get_error() != OK:
        push_error("GDPP: cannot recover '%s' after an interrupted export" % path)
        return false
    DirAccess.remove_absolute(ProjectSettings.globalize_path(backup_path))
    return true


func _exit_tree() -> void:
    remove_tool_menu_item("Build GDPP Native Library")
    if _export_plugin != null:
        remove_export_plugin(_export_plugin)
        _export_plugin = null
    if _build_progress != null:
        _build_progress.queue_free()
        _build_progress = null
    _compiler = null


func _define_setting(name: String, default_value: Variant) -> void:
    if not ProjectSettings.has_setting(name):
        ProjectSettings.set_setting(name, default_value)
    ProjectSettings.set_initial_value(name, default_value)


func _default_sdk_root() -> String:
    for version: String in _compiler.get_supported_godot_versions():
        var version_root := "res://addons/gdpp/sdk/%s" % version
        var single_host_manifest := version_root.path_join("sdk.manifest")
        var complete_host_manifest := version_root.path_join(
            "%s/%s/sdk.manifest" % [
                _compiler.get_host_platform(),
                _compiler.get_host_architecture(),
            ]
        )
        if (
            FileAccess.file_exists(single_host_manifest)
            or FileAccess.file_exists(complete_host_manifest)
        ):
            return ProjectSettings.globalize_path("res://addons/gdpp/sdk")
    return _compiler.get_default_sdk_root()


func _default_android_ndk_root() -> String:
    for environment_name in ["ANDROID_NDK_ROOT", "ANDROID_NDK_HOME"]:
        var configured := OS.get_environment(environment_name)
        if FileAccess.file_exists(configured.path_join("build/cmake/android.toolchain.cmake")):
            return configured

    var sdk_root := OS.get_environment("ANDROID_HOME")
    if sdk_root.is_empty():
        sdk_root = OS.get_environment("ANDROID_SDK_ROOT")
    if sdk_root.is_empty():
        var home := OS.get_environment("USERPROFILE") if OS.get_name() == "Windows" else OS.get_environment("HOME")
        if OS.get_name() == "macOS":
            sdk_root = home.path_join("Library/Android/sdk")
        elif OS.get_name() == "Windows":
            sdk_root = home.path_join("AppData/Local/Android/Sdk")
        else:
            sdk_root = home.path_join("Android/Sdk")

    var ndk_parent := sdk_root.path_join("ndk")
    # DirAccess.get_directories_at() reports a Godot ERROR for a missing path. Android is an
    # optional export target, so a clean desktop-only installation must simply leave this
    # setting empty instead of poisoning editor/import logs.
    if not DirAccess.dir_exists_absolute(ndk_parent):
        return ""
    var versions := DirAccess.get_directories_at(ndk_parent)
    versions.sort()
    for index in range(versions.size() - 1, -1, -1):
        var candidate := ndk_parent.path_join(versions[index])
        if FileAccess.file_exists(candidate.path_join("build/cmake/android.toolchain.cmake")):
            return candidate
    return ""


func _detected_target_version() -> String:
    var engine := Engine.get_version_info()
    return "%d.%d" % [int(engine.get("major", 0)), int(engine.get("minor", 0))]


func _compile_project() -> void:
    if _building:
        push_warning("GDPP: a native build is already running")
        return
    _building = true
    var target_version := str(ProjectSettings.get_setting(TARGET_VERSION_SETTING, "4.4"))
    var progress_callback := Callable()
    if _build_progress != null and _build_progress.is_available():
        _build_progress.begin(PackedStringArray(["development"]))
        _build_progress.set_active_stage("development")
        progress_callback = Callable(self, "_on_manual_build_progress")
    print("GDPP: analyzing project scripts for Godot %s..." % target_version)
    var job := NATIVE_BUILD_JOB.new()
    var frame_callback := Callable()
    if _build_progress != null and _build_progress.is_available():
        frame_callback = Callable(_build_progress, "refresh")
    var outcome: Dictionary = job.run(_compiler, {
        "project_root": "res://",
        "output_directory": OUTPUT_DIRECTORY,
        "sdk_root": ProjectSettings.get_setting(SDK_SETTING),
        "compiler_executable": ProjectSettings.get_setting(COMPILER_SETTING),
        "target_version": target_version,
        "build_profile": "development",
        "target_platform": _compiler.get_host_platform(),
        "target_architecture": _compiler.get_host_architecture(),
        "target_variant": "",
    }, progress_callback, frame_callback)
    var result: Dictionary = outcome.get("plan", {})
    if not result.get("success", false):
        for diagnostic in result.get("diagnostics", []):
            push_error(diagnostic)
        _finish_manual_build_progress()
        _building = false
        return

    print(
        "GDPP: %d compiled, %d cached, %d removed" % [
            result.compiled_count,
            result.cache_hit_count,
            result.removed_count,
        ]
    )
    if not _accept_project_build(outcome.get("execution", {})):
        _finish_manual_build_progress()
        _building = false
        return

    EditorInterface.get_resource_filesystem().scan()
    if result.get("native_up_to_date", false):
        print("GDPP: native project library is up to date")
    else:
        print("GDPP: native project library built successfully")
    _finish_manual_build_progress()
    _building = false


func _accept_project_build(execution: Dictionary) -> bool:
    if not execution.get("success", false):
        for diagnostic in execution.get("diagnostics", []):
            push_error("GDPP: %s" % diagnostic)
        return false
    for diagnostic in execution.get("diagnostics", []):
        push_warning("GDPP: %s" % diagnostic)
    var removed := int(execution.get("removed_count", 0))
    if removed > 0:
        print("GDPP: removed %d stale development project libraries" % removed)
    return true


func _on_manual_build_progress(phase: String, completed: int, total: int) -> void:
    if _build_progress != null:
        _build_progress.update("development", phase, completed, total)


func _finish_manual_build_progress() -> void:
    if _build_progress != null and _build_progress.is_available():
        _build_progress.finish()
