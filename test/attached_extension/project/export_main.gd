extends "res://vendor_grandchild.gd"

const DEFERRED_PHYSICS_SCENE := preload("res://deferred_shape.tscn")
const INNER_DATA := preload("res://inner_data.gd")


func _ready() -> void:
    super._ready()
    call_deferred(&"_verify_export_runtime")


func _verify_export_runtime() -> void:
    if not is_class(&"VendorBase"):
        _fail("export changed the provider-owned Node type")
        return
    if compute(3) != 62 or invoke_hook(4) != 54:
        _fail("export changed attached inheritance or native dispatch")
        return
    if get_ready_notifications() != 1:
        _fail("export did not preserve the provider lifecycle callback")
        return

    var data := load("res://vendor_data.tres")
    if data == null or not data.is_class(&"VendorResource"):
        _fail("export changed the provider-owned Resource type")
        return
    if data.compute(2) != 62:
        _fail("export changed attached Resource fields or native dispatch")
        return

    var physics_fixture := DEFERRED_PHYSICS_SCENE.instantiate()
    if physics_fixture == null:
        _fail("deferred attached-script preload did not materialize at runtime")
        return
    var collision_shape := physics_fixture.get_node("CollisionShape2D") as CollisionShape2D
    if collision_shape == null or not collision_shape.shape is CircleShape2D:
        _fail("deferred attached-script preload lost its physics resource")
        return
    physics_fixture.queue_free()

    var reflected_constants: Dictionary = get_script().get_script_constant_map()
    if reflected_constants.get(&"DEFERRED_PHYSICS_SCENE") != DEFERRED_PHYSICS_SCENE:
        _fail("attached Script reflection did not materialize a local deferred constant")
        return
    if reflected_constants.get(&"INHERITED_PHYSICS_SCENE") != INHERITED_PHYSICS_SCENE:
        _fail("attached Script reflection did not inherit a deferred constant")
        return

    var dynamic_entry: Variant = INNER_DATA.Entry.new()
    dynamic_entry.count = 42
    dynamic_entry.label = "attached"
    var indexed_entries: Dictionary[int, Variant] = {}
    indexed_entries[dynamic_entry.count] = dynamic_entry
    if (
        dynamic_entry.count != 42
        or dynamic_entry.label != "attached"
        or indexed_entries.get(42) != dynamic_entry
    ):
        _fail("dynamic attached inner-class properties lost typed getter/setter semantics")
        return

    print("GDPP_ATTACHED_EXPORT_RUNTIME_OK")
    get_tree().quit(0)


func _fail(message: String) -> void:
    push_error("GDPP attached export runtime: %s" % message)
    get_tree().quit(1)
