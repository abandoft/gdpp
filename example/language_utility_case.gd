extends RefCounted


func run() -> Dictionary:
    var node := Node.new()
    var report := {
        "converted": convert("42", TYPE_INT),
        "types_exist": [type_exists(&"Node"), type_exists(&"GDPPMissingType")],
        "character": char(0x1f642),
        "ordinal": ord("🙂"),
        "colors": [Color8(255, 128, 0), Color8(255, 128, 0, 64)],
        "instances": [
            is_instance_of(42, TYPE_INT),
            is_instance_of(42.0, TYPE_INT),
            is_instance_of(node, Node),
            is_instance_of(null, Node),
        ],
    }
    node.free()
    return report
