@tool
extends RefCounted
class_name ToolModeCase

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


static func runtime_instance_available() -> bool:
    return RuntimeModeCase.new() != null


static func runtime_static_contract() -> Array:
    return [RuntimeModeCase.RUNTIME_CONSTANT, RuntimeModeCase.pure_static_answer()]


static func runtime_static_field_is_null() -> bool:
    return RuntimeModeCase.static_initialized == null


func instance_ready() -> bool:
    return initialized
