extends SceneTree


func _init() -> void:
    call_deferred("_run")


func _native_class_for(source_path: String) -> StringName:
    var manifest := FileAccess.get_file_as_string(
        "res://addons/gdpp/build/project/manifest.txt"
    )
    for line in manifest.split("\n", false):
        var fields := line.split('"')
        if fields.size() >= 8 and fields[1] == source_path:
            return StringName(fields[7])
    return &""


func _fail(message: String) -> void:
    push_error(message)
    quit(1)


func _run() -> void:
    if not Engine.is_editor_hint():
        _fail("Tool execution smoke did not start in editor mode")
        return

    while EditorInterface.get_resource_filesystem().is_scanning():
        await process_frame

    var tool_class := _native_class_for("tool_mode_case.gd")
    var runtime_class := _native_class_for("runtime_mode_case.gd")
    if tool_class.is_empty() or runtime_class.is_empty():
        _fail("Tool execution classes are absent from the native manifest")
        return
    if not ClassDB.class_call_static(tool_class, &"static_ready"):
        _fail("@tool static initialization did not execute in the editor")
        return

    var tool_count_before := int(
        ClassDB.class_call_static(tool_class, &"constructed_instances")
    )
    var tool_instance: Object = ClassDB.instantiate(tool_class)
    if tool_instance == null or not tool_instance.call(&"instance_ready"):
        _fail("@tool instance initialization did not execute in the editor")
        return
    var tool_count_after := int(
        ClassDB.class_call_static(tool_class, &"constructed_instances")
    )
    if tool_count_after != tool_count_before + 1:
        _fail("@tool editor construction did not execute exactly once")
        return

    var runtime_count_before := int(
        ClassDB.class_call_static(runtime_class, &"constructed_instances")
    )
    var runtime_placeholder: Object = ClassDB.instantiate(runtime_class)
    if runtime_placeholder == null:
        _fail("Non-tool ClassDB placeholder was not created in the editor")
        return
    var runtime_count_after := int(
        ClassDB.class_call_static(runtime_class, &"constructed_instances")
    )
    if runtime_count_after != runtime_count_before:
        _fail("Non-tool native constructor executed in the editor")
        return

    tool_instance = null
    runtime_placeholder = null
    print("GDPP_TOOL_EDITOR_OK")
    quit(0)
