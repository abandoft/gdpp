extends SceneTree


func _init() -> void:
    call_deferred("_run")


func _fail(message: String) -> void:
    push_error("GDPP attached compile: %s" % message)
    quit(1)


func _run() -> void:
    if not ClassDB.class_exists(&"VendorBase"):
        _fail("independent vendor extension was not loaded")
        return
    if not ClassDB.class_exists(&"GDPPCompiler"):
        _fail("compiler extension was not loaded")
        return

    var compiler := GDPPCompiler.new()
    var engine := Engine.get_version_info()
    var target_version := "%d.%d" % [int(engine.major), int(engine.minor)]
    var result: Dictionary = compiler.compile_project(
        "res://",
        "res://addons/gdpp/build/project",
        ProjectSettings.globalize_path("res://addons/gdpp/sdk"),
        compiler.get_default_compiler_executable(),
        target_version
    )
    if not result.get("success", false):
        _fail("planning failed: %s" % result.get("diagnostics", []))
        return
    var attached_bases: Dictionary = result.get("attached_script_bases", {})
    var expected_bases := {
        "res://vendor_child.gd": "VendorBase",
        "res://vendor_grandchild.gd": "VendorBase",
        "res://vendor_data.gd": "VendorResource",
    }
    for path: String in expected_bases:
        if attached_bases.get(path, "") != expected_bases[path]:
            _fail("missing attachment metadata for %s: %s" % [path, attached_bases])
            return

    var execution: Dictionary = compiler.execute_project_build(result)
    if not execution.get("success", false):
        _fail("native build failed: %s" % execution.get("diagnostics", []))
        return
    if not FileAccess.file_exists(str(result.get("output_library", ""))):
        _fail("native project library was not produced")
        return

    var descriptor_path := "res://addons/gdpp/build/project/gdpp_project.gdextension"
    var descriptor := FileAccess.get_file_as_string(descriptor_path)
    if "reloadable = false" not in descriptor:
        _fail("attached runtime descriptor is incorrectly reloadable")
        return
    var registry := FileAccess.open("res://.godot/extension_list.cfg", FileAccess.WRITE)
    if registry == null:
        _fail("cannot prepare runtime extension registry")
        return
    registry.store_string(
        "res://addons/vendor/vendor.gdextension\n"
        + descriptor_path + "\n"
    )
    if registry.get_error() != OK:
        _fail("cannot commit runtime extension registry")
        return
    print("GDPP_ATTACHED_COMPILE_OK")
    quit(0)
