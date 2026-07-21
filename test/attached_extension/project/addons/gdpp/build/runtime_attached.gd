extends SceneTree

var _child_ping := -1
var _vendor_ping := -1


func _init() -> void:
    call_deferred("_run")


func _fail(message: String) -> void:
    push_error("GDPP attached runtime: %s" % message)
    quit(1)


func _compiled_script(path: String) -> Script:
    var script := ClassDB.instantiate(&"AttachedCompiledScript") as Script
    if script != null:
        script.set("source_path", path)
    return script


func _on_child_ping(value: int) -> void:
    _child_ping = value


func _on_vendor_ping(value: int) -> void:
    _vendor_ping = value


func _run() -> void:
    if ClassDB.class_exists(&"GDPPCompiler"):
        _fail("compiler-only extension leaked into the runtime process")
        return
    for required_class in [&"VendorBase", &"AttachedCompiledScript"]:
        if not ClassDB.class_exists(required_class):
            _fail("runtime class '%s' is unavailable" % required_class)
            return

    var node := ClassDB.instantiate(&"VendorBase") as Node
    var script := _compiled_script("res://vendor_grandchild.gd")
    if node == null or script == null or script.get_instance_base_type().is_empty():
        _fail("cannot construct attached runtime objects")
        return
    node.set_script(script)
    if node.get_script() != script or not node.is_class(&"VendorBase"):
        _fail("compiled script did not preserve the provider-owned object")
        return
    if int(node.get("initialized")) != 1:
        _fail("compiled _init was not invoked exactly once")
        return

    node.set("native_bias", 11)
    node.set("bonus", 13)
    node.set("extra", 5)
    if int(node.call("compute", 3)) != 32:
        _fail("script-to-script and native super dispatch returned the wrong value")
        return
    if int(node.call("invoke_hook", 4)) != 30:
        _fail("provider-to-script virtual dispatch returned the wrong value")
        return
    node.connect(&"child_ping", _on_child_ping)
    node.connect(&"vendor_ping", _on_vendor_ping)
    node.call("emit_vendor_ping", 41)
    if _vendor_ping != 41:
        _fail("provider signal was not preserved")
        return

    get_root().add_child(node)
    await process_frame
    if not bool(node.get("ready_seen")) or int(node.call("get_ready_notifications")) != 1:
        _fail("provider and compiled lifecycle callbacks did not both run")
        return
    if _child_ping != 13:
        _fail("compiled script signal was not emitted")
        return
    get_root().remove_child(node)

    node.name = "RoundTrip"
    var packed := PackedScene.new()
    if packed.pack(node) != OK:
        _fail("cannot pack provider-owned attached node")
        return
    var roundtrip_path := "res://attached_roundtrip.scn"
    if ResourceSaver.save(packed, roundtrip_path) != OK:
        _fail("cannot serialize attached compiled script")
        return
    node.free()
    var loaded := ResourceLoader.load(
        roundtrip_path,
        "PackedScene",
        ResourceLoader.CACHE_MODE_IGNORE
    ) as PackedScene
    var restored := loaded.instantiate() if loaded != null else null
    if restored == null:
        _fail("cannot restore serialized attached scene")
        return
    if not restored.is_class(&"VendorBase"):
        _fail("serialized scene changed the provider native type")
        return
    if not restored.get_script() is Script:
        _fail("serialized scene lost the compiled script resource")
        return
    if str(restored.get_script().get("source_path")) != "res://vendor_grandchild.gd":
        _fail("serialized compiled script lost its descriptor path")
        return
    if int(restored.call("compute", 3)) != 32:
        _fail("serialized fields or super dispatch changed after round trip")
        return
    restored.free()

    var data := ClassDB.instantiate(&"VendorResource") as Resource
    var data_script := _compiled_script("res://vendor_data.gd")
    if data == null or data_script == null:
        _fail("cannot construct provider-owned attached resource")
        return
    data.set_script(data_script)
    data.set("native_bias", 29)
    data.set("bonus", 31)
    if int(data.call("compute", 2)) != 62:
        _fail("resource native super dispatch returned the wrong value")
        return
    var resource_roundtrip_path := "res://attached_roundtrip.res"
    if ResourceSaver.save(data, resource_roundtrip_path) != OK:
        _fail("cannot serialize provider-owned attached resource")
        return
    data = null
    var restored_data := ResourceLoader.load(
        resource_roundtrip_path,
        "",
        ResourceLoader.CACHE_MODE_IGNORE
    )
    if restored_data == null or not restored_data.is_class(&"VendorResource"):
        _fail("serialized resource changed the provider native type")
        return
    if str(restored_data.get_script().get("source_path")) != "res://vendor_data.gd":
        _fail("serialized resource lost its compiled script descriptor")
        return
    if int(restored_data.call("compute", 2)) != 62:
        _fail("serialized resource fields changed after round trip")
        return
    restored_data = null
    DirAccess.remove_absolute(ProjectSettings.globalize_path(roundtrip_path))
    DirAccess.remove_absolute(ProjectSettings.globalize_path(resource_roundtrip_path))
    print("GDPP_ATTACHED_RUNTIME_OK")
    quit(0)
