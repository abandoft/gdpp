extends RefCounted
class_name CrossContainerOwner

var values: Array[CrossContainerValue] = []


func store(value: CrossContainerValue) -> void:
    values.push_back(value)
