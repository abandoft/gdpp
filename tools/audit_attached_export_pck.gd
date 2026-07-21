extends SceneTree

const EXTENSION_REGISTRY := "res://.godot/extension_list.cfg"
const VENDOR_DESCRIPTOR := "res://addons/vendor/vendor.gdextension"
const PROJECT_DESCRIPTOR := "res://addons/gdpp/gdpp_project.gdextension"
const HARNESS_FILES := {
    "res://audit_attached_export_pck.gd": true,
    "res://audit_attached_export_pck.gd.uid": true,
}


func _init() -> void:
    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 1:
        push_error("usage: audit_attached_export_pck.gd -- <export.pck>")
        quit(2)
        return
    var package_path := ProjectSettings.globalize_path(arguments[0])
    if not ProjectSettings.load_resource_pack(package_path, true):
        push_error("cannot mount attached export PCK: %s" % package_path)
        quit(3)
        return

    var files: PackedStringArray = []
    var violations: PackedStringArray = []
    if not _collect_files("res://", files):
        quit(4)
        return
    var transformed_scene_count := 0
    var transformed_resource_count := 0
    for path in files:
        if HARNESS_FILES.has(path):
            continue
        var lower := path.to_lower()
        var extension := lower.get_extension()
        if extension in ["gd", "gdc"]:
            violations.append("export contains GDScript source or bytecode: %s" % path)
        if extension in ["c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx"]:
            violations.append("export contains native source: %s" % path)
        if lower.contains("gdpp_compiler") or lower.contains("gdpp_fallback"):
            violations.append("export contains a compiler-only library: %s" % path)
        if path.begins_with("res://addons/gdpp/runtime/scenes/"):
            transformed_scene_count += 1
        elif path.begins_with("res://addons/gdpp/runtime/resources/"):
            transformed_resource_count += 1

    _audit_extension_registry(violations)
    if not FileAccess.file_exists(VENDOR_DESCRIPTOR):
        violations.append("export omitted the independent vendor descriptor")
    if not FileAccess.file_exists(PROJECT_DESCRIPTOR):
        violations.append("export omitted the GDPP project descriptor")
    if transformed_scene_count == 0:
        violations.append("export contains no transformed attached scene")
    if transformed_resource_count == 0:
        violations.append("export contains no transformed attached resource")

    print("GDPP_ATTACHED_PCK_FILES=%d" % files.size())
    print("GDPP_ATTACHED_PCK_SCENES=%d" % transformed_scene_count)
    print("GDPP_ATTACHED_PCK_RESOURCES=%d" % transformed_resource_count)
    print("GDPP_ATTACHED_PCK_VIOLATIONS=%d" % violations.size())
    for violation in violations:
        push_error(violation)
    quit(0 if violations.is_empty() else 5)


func _audit_extension_registry(violations: PackedStringArray) -> void:
    if not FileAccess.file_exists(EXTENSION_REGISTRY):
        violations.append("export omitted the runtime extension registry")
        return
    var entries := FileAccess.get_file_as_string(EXTENSION_REGISTRY).split("\n", false)
    if entries.has("res://addons/gdpp/gdpp.gdextension"):
        violations.append("runtime registry contains the compiler extension")
    var vendor_index := entries.find(VENDOR_DESCRIPTOR)
    var project_index := entries.find(PROJECT_DESCRIPTOR)
    if vendor_index < 0:
        violations.append("runtime registry omitted the vendor extension")
    if project_index < 0:
        violations.append("runtime registry omitted the GDPP project extension")
    if vendor_index >= 0 and project_index >= 0 and vendor_index >= project_index:
        violations.append("runtime registry loads GDPP before its vendor provider")


func _collect_files(directory_path: String, output: PackedStringArray) -> bool:
    var directory := DirAccess.open(directory_path)
    if directory == null:
        push_error("cannot enumerate mounted PCK directory: %s" % directory_path)
        return false
    directory.list_dir_begin()
    while true:
        var name := directory.get_next()
        if name.is_empty():
            break
        if name in [".", ".."]:
            continue
        var path := directory_path.path_join(name)
        if directory.current_is_dir():
            if not _collect_files(path, output):
                directory.list_dir_end()
                return false
        else:
            output.append(path)
    directory.list_dir_end()
    return true
