extends RefCounted

signal changed


func _marker() -> void:
    pass


func truthiness_trace() -> Array[bool]:
    var missing: Object = null
    return [
        not null,
        not false,
        not true,
        not 0,
        not 1,
        not 0.0,
        not NAN,
        not "",
        not "filled",
        not StringName(),
        not &"filled",
        not NodePath(),
        not ^"root/child",
        not Vector2(),
        not Vector2(1.0, 0.0),
        not Rect2(),
        not Transform2D(),
        not Color(),
        not Color(1.0, 0.0, 0.0, 0.0),
        not RID(),
        not Callable(),
        not _marker,
        not Signal(),
        not changed,
        not {},
        not {"value": 0},
        not [],
        not [0],
        not PackedByteArray(),
        not PackedByteArray([0]),
        not missing,
        not self,
    ]


func conversion_trace(
    texture: Texture2D,
    values: Array,
    strings: PackedStringArray,
    vector_i: Vector2i,
    rect_i: Rect2i,
    basis: Basis,
    projection: Projection,
    typed_values: Array[int],
    typed_dictionary: Dictionary[String, int],
) -> Dictionary:
    var text: String = ^"root/child"
    var name: StringName = text
    var path: NodePath = text
    var vector: Vector2 = vector_i
    var rect: Rect2 = rect_i
    var rotation: Quaternion = basis
    var transform: Transform3D = projection
    var packed: PackedInt64Array = values
    var unpacked: Array = strings
    var typed: Array[int] = [3, 4]
    var color: Color = 0xff8040ff
    var handle: RID = texture.get_rid()
    var enabled: bool = 2
    var truncated: int = 2.75
    var parse_source: String = "42"
    var parsed: int = parse_source as int
    var dynamic_values: Variant = typed_values
    var restored_values: Array[int] = dynamic_values
    var dynamic_dictionary: Variant = typed_dictionary
    var restored_dictionary: Dictionary[String, int] = dynamic_dictionary
    return {
        "text": text,
        "name": name,
        "path": path,
        "vector": vector,
        "rect": rect,
        "rotation": rotation,
        "transform": transform,
        "packed": packed,
        "unpacked": unpacked,
        "typed": typed,
        "color": color,
        "handle": handle,
        "enabled": enabled,
        "truncated": truncated,
        "parsed": parsed,
        "restored_values": restored_values,
        "restored_dictionary": restored_dictionary,
    }
