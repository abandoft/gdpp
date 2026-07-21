class_name VendorData
extends VendorResource

@export var bonus: int = 4


func compute(value: int) -> int:
    return super.native_compute(value) + bonus
