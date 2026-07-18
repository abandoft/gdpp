extends RefCounted
class_name IterationCase


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

    var typed: int
    var dynamic
    var object: Node
    trace.append_array([typed, dynamic, object == null])
    return trace
