extends SceneTree

const HARNESS_FILES := {
    "res://audit_export_pck.gd": true,
    "res://audit_export_pck.gd.uid": true,
    "res://inspect_export_pck.gd": true,
    "res://inspect_export_pck.gd.uid": true,
}


func _init() -> void:
    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 1:
        push_error("用法：godot --headless --path <空项目> --script inspect_export_pck.gd -- <导出包.pck>")
        quit(2)
        return

    var package_path := ProjectSettings.globalize_path(arguments[0])
    if not ProjectSettings.load_resource_pack(package_path, true):
        push_error("无法加载 PCK：%s" % package_path)
        quit(3)
        return

    var files: PackedStringArray = []
    if not _collect_files("res://", files):
        quit(4)
        return

    var extension_counts: Dictionary = {}
    var extension_bytes: Dictionary = {}
    var script_files: PackedStringArray = []
    var script_bytes := 0
    var total_files := 0
    var total_bytes := 0
    var gdpp_files := 0
    var gdpp_bytes := 0
    for path in files:
        if HARNESS_FILES.has(path):
            continue
        var size := _file_size(path)
        var extension := path.get_extension().to_lower()
        if extension.is_empty():
            extension = "<none>"
        extension_counts[extension] = int(extension_counts.get(extension, 0)) + 1
        extension_bytes[extension] = int(extension_bytes.get(extension, 0)) + size
        total_files += 1
        total_bytes += size
        if extension == "gd" or extension == "gdc":
            script_files.append(path)
            script_bytes += size
        if path.begins_with("res://addons/gdpp/"):
            gdpp_files += 1
            gdpp_bytes += size

    script_files.sort()
    var result := {
        "package": arguments[0],
        "files": total_files,
        "uncompressed_bytes": total_bytes,
        "extensions": _sorted_extension_records(extension_counts, extension_bytes),
        "script_files": script_files,
        "script_file_count": script_files.size(),
        "script_bytes": script_bytes,
        "gdpp_file_count": gdpp_files,
        "gdpp_bytes": gdpp_bytes,
    }
    print("PCK_INSPECTION_JSON=" + JSON.stringify(result))
    quit(0)


func _collect_files(directory_path: String, output: PackedStringArray) -> bool:
    var directory := DirAccess.open(directory_path)
    if directory == null:
        push_error("无法读取 PCK 目录：%s" % directory_path)
        return false
    directory.list_dir_begin()
    while true:
        var name := directory.get_next()
        if name.is_empty():
            break
        if name == "." or name == "..":
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


func _file_size(path: String) -> int:
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        return 0
    return file.get_length()


func _sorted_extension_records(counts: Dictionary, sizes: Dictionary) -> Array[Dictionary]:
    var extensions: Array = counts.keys()
    extensions.sort()
    var records: Array[Dictionary] = []
    for extension: String in extensions:
        records.append({
            "extension": extension,
            "files": int(counts[extension]),
            "bytes": int(sizes[extension]),
        })
    return records
