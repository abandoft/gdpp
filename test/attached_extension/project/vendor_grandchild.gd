class_name VendorGrandchild
extends "res://vendor_child.gd"

@export var extra: int = 2


func compute(value: int) -> int:
    return super.compute(value) + extra


func hook(value: int) -> int:
    return super.hook(value) + extra
