extends Node
class_name VirtualSignatureCase

var trace: Array = []


func _process(delta: Variant = 1.25, context = null) -> void:
    trace.append([delta, context])


func run() -> Array:
    _process()
    _process(2.5, "manual")
    return trace.duplicate()
