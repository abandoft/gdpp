extends SceneTree


func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    var compiler := GDPPCompiler.new()
    var result: Dictionary = compiler.compile_source(
        "extends Node\nclass_name NativeSmoke\nfunc answer() -> int:\n    return 42\n",
        "native_smoke.gd"
    )
    if not result.get("success", false):
        push_error("GDPP smoke compilation failed: %s" % result.get("diagnostics", []))
        quit(1)
        return
    if "GDCLASS(GDPPNative_NativeSmoke, godot::Node)" not in result.get("header", ""):
        push_error("GDPP generated an unexpected class")
        quit(1)
        return
    var optimization: Dictionary = result.get("optimization", {})
    if optimization.get("constants_folded", -1) < 0:
        push_error("GDPP did not expose optimization statistics")
        quit(1)
        return
    print("GDPP_SMOKE_OK")
    quit(0)
