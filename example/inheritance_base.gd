extends Node
class_name InheritanceBase

const BASE_BONUS: int = 2
enum SharedMode { IDLE, ACTIVE = 4, BOOST = ACTIVE * 2 }
enum { ANON_LIMIT = 11 }
signal override_resumed

var inherited_value: int = 40


func _init(value: int = 40) -> void:
    inherited_value = value


func inherited_answer() -> int:
    return inherited_value + 2


func overridable_answer() -> int:
    return -1


static func static_answer() -> int:
    return 42
