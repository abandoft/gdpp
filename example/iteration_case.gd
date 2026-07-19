extends RefCounted
class_name IterationCase


class CounterIterator:
    var count: int

    func _init(value: int) -> void:
        count = value

    func _iter_init(state: Array) -> bool:
        state[0] = 0
        return count > 0

    func _iter_next(state: Array) -> bool:
        state[0] += 1
        return state[0] < count

    func _iter_get(state: Variant) -> StringName:
        return StringName("custom_%d" % int(state))


func run() -> Array:
    var trace: Array = []
    var values := [1, 2]
    for value: int in values:
        trace.append(value)
        if value == 1:
            values.append(3)

    var packed := PackedInt64Array([4, 5])
    for value: int in packed:
        trace.append(value)
        if value == 4:
            packed.append(6)

    var text := "A🙂B"
    for character: String in text:
        trace.append(character)
        if character == "A":
            text += "C"

    var labels: Dictionary[String, int] = {"left": 1, "right": 2}
    for key: String in labels:
        trace.append(key)
        if key == "left":
            labels["tail"] = 3

    for value: float in 3.2:
        trace.append(value)
    for value: float in Vector2(1.5, 4.0):
        trace.append(value)
    for value: int in Vector2i(2, 5):
        trace.append(value)
    for value: float in Vector3(5.0, 0.0, -2.0):
        trace.append(value)
    for value: int in Vector3i(7, 0, -3):
        trace.append(value)
    for value: int in Vector3i(0, 10, 0):
        trace.append(value)

    for value: float in [8, 9]:
        trace.append(value)
    for key: StringName in { alpha = 1, beta = 2 }:
        trace.append(key)
    for value in CounterIterator.new(3):
        trace.append(value)

    var typed: int
    var dynamic
    var object: Node
    trace.append_array([typed, dynamic, object == null])
    return trace
