extends RefCounted


var dynamic_values: Dictionary = {}
var typed_values: Dictionary[StringName, RefCounted] = {}
static var shared_values: Dictionary = {}
var text: String = "stable"
var key: StringName = &"stable"
var path: NodePath = ^"stable"
var values: Array[String] = ["stable"]
var bytes: PackedByteArray = PackedByteArray([1, 2, 3])
var callback: Callable = self_assign_wrappers


func replace_dynamic(value: RefCounted) -> void:
    dynamic_values = {&"value": value}


func self_assign_dynamic() -> void:
    dynamic_values = dynamic_values


func clear_dynamic() -> void:
    dynamic_values = {}


func replace_typed(value: RefCounted) -> void:
    typed_values = {&"value": value}


func clear_typed() -> void:
    typed_values = {}


static func replace_shared(value: RefCounted) -> void:
    shared_values = {&"value": value}


static func clear_shared() -> void:
    shared_values = {}


func self_assign_wrappers() -> bool:
    text = text
    key = key
    path = path
    values = values
    bytes = bytes
    callback = callback
    return (
        text == "stable"
        and key == &"stable"
        and path == ^"stable"
        and values == ["stable"]
        and bytes == PackedByteArray([1, 2, 3])
        and callback.is_valid()
    )
