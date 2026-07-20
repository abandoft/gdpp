extends RefCounted
class_name RuntimeModeCase

static var static_initialized: bool = false
static var instance_count: int = 0
var initialized: bool = false


static func _static_init() -> void:
    static_initialized = true


func _init() -> void:
    initialized = true
    instance_count += 1


static func static_ready() -> bool:
    return static_initialized


static func constructed_instances() -> int:
    return instance_count


func instance_ready() -> bool:
    return initialized
