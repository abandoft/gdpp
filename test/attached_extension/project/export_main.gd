extends "res://vendor_grandchild.gd"


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

    print("GDPP_ATTACHED_EXPORT_RUNTIME_OK")
    get_tree().quit(0)


func _fail(message: String) -> void:
    push_error("GDPP attached export runtime: %s" % message)
    get_tree().quit(1)
