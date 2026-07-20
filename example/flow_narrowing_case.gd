extends RefCounted


func _object_name(value: Variant) -> String:
    if value is Node:
        return value.name
    return ""


func _short_circuit(value: Variant) -> bool:
    return value is int and value > 0


func _deep_object(value: Variant) -> bool:
    return value is Node and value is Node2D and value.position == Vector2(3.0, 4.0)


func _after_guard(value: Variant) -> String:
    if value is not Node:
        return ""
    return value.name


func _conditional(value: Variant) -> String:
    return value.name if value is Node else ""


func _match_size(value: Variant) -> int:
    match value:
        [_, _, _]:
            return value.size()
        {"answer": _}:
            return value.size()
        _:
            return -1


func _guarded_binding(value: Variant) -> String:
    match value:
        var item when item is Node:
            return item.name
        _:
            return ""


func _reassigned(value: Variant) -> int:
    if value is Node:
        value = 40
        value += 2
        return value
    return -1


func _non_null(value: Node) -> String:
    if value != null:
        return value.name
    return ""


func run() -> Dictionary:
    var node: Variant = Node2D.new()
    node.name = "flow-node"
    node.position = Vector2(3.0, 4.0)
    var result := {
        "object": _object_name(node),
        "short_circuit": _short_circuit(7),
        "deep_object": _deep_object(node),
        "postdominator": _after_guard(node),
        "conditional": _conditional(node),
        "array_match": _match_size([1, 2, 3]),
        "dictionary_match": _match_size({"answer": 42}),
        "guarded_binding": _guarded_binding(node),
        "assignment_invalidation": _reassigned(node),
        "non_null": _non_null(node),
    }
    node.free()
    return result
