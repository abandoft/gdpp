extends Node
class_name RequiredInitializer

var initialized_value: int = 0


func _init(value: int) -> void:
    initialized_value = value


func answer() -> int:
    return initialized_value
