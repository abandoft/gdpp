class_name VendorChild
extends VendorBase

signal child_ping(value: int)

@export var bonus: int = 7
var initialized: int = 0
var ready_seen: bool = false


func _init() -> void:
    initialized += 1


func _ready() -> void:
    ready_seen = true
    child_ping.emit(bonus)


func compute(value: int) -> int:
    return super.native_compute(value) + bonus


func combine(base: int, optional: int = 2, ...values: Array) -> int:
    return base + optional + values.size()


func hook(value: int) -> int:
    return value * 3 + bonus


@rpc("any_peer", "call_local", "reliable", 2)
func inherited_rpc(value: int) -> int:
    return value + bonus


@rpc("authority", "call_remote", "unreliable", 1)
func overridden_rpc(value: int) -> int:
    return value
