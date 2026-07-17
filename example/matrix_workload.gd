extends Node2D
class_name GDPPMatrixWorkload

signal pulse(value: int)

var checksum: int = 0
var signal_total: int = 0


func _ready() -> void:
    pulse.connect(_on_pulse)


func _on_pulse(value: int) -> void:
    signal_total += value


func run_oracle() -> Dictionary:
    var integers: int = 0
    for value in range(1, 101):
        integers += value * value
    var values: Array[int] = [1, 2, 3, 4]
    values[2] += 7
    var record: Dictionary = {"name": "gdpp", "nested": {"score": 7}}
    record.nested.score += values[2]
    var mode := ""
    match record.nested.score:
        17:
            mode = "exact"
        _:
            mode = "unexpected"
    var triple := func(value: int) -> int: return value * 3
    var dynamic_maximum: Variant = 0x7fffffffffffffff
    var dynamic_one: Variant = 1
    position = Vector2(3.5, -2.25)
    signal_total = 0
    pulse.emit(4)
    pulse.emit(9)
    return {
        "integer_sum": integers,
        "array": values,
        "nested_score": record.nested.score,
        "match": mode,
        "lambda": triple.call(14),
        "variant_overflow": dynamic_maximum + dynamic_one,
        "variant_bit_and": dynamic_maximum & 0x55aa55aa55aa55aa,
        "position": [position.x, position.y],
        "signal_total": signal_total,
        "string": ("GDS" + "cript").to_upper(),
    }


func _numeric_batch(iterations: int) -> void:
    var total: int = checksum
    for value in range(iterations):
        total = (total * 33 + value) & 0x7fffffff
    checksum = total


func _array_batch(iterations: int) -> void:
    var values: Array[int] = [1, 3, 5, 7, 9, 11, 13, 15]
    var total: int = checksum
    for value in range(iterations):
        var index := value & 7
        values[index] += value & 3
        total += values[index]
    checksum = total


func _dictionary_batch(iterations: int) -> void:
    var values := {"a": 1, "b": 2, "c": 3, "d": 4}
    var total: int = checksum
    for value in range(iterations):
        values.a += value & 1
        values.c += values.a & 3
        total += values.a + values.c
    checksum = total


func _method_batch(iterations: int) -> void:
    var total: int = checksum
    for value in range(iterations):
        total += _mix(value, total)
    checksum = total


func _mix(value: int, previous: int) -> int:
    return (value * 17) ^ (previous & 255)


func _variant_batch(iterations: int) -> void:
    var value: Variant = 1
    var total: int = checksum
    for index in range(iterations):
        value = int(value) + (index & 7)
        total += int(value)
    checksum = total


func _signal_batch(iterations: int) -> void:
    signal_total = 0
    for value in range(iterations):
        pulse.emit(value & 3)
    checksum += signal_total


func _branch_batch(iterations: int) -> void:
    var total: int = checksum
    for value in range(iterations):
        if (value & 3) == 0:
            total += value
        elif (value & 1) == 0:
            total -= value
        else:
            total ^= value
    checksum = total


func _packed_array_batch(iterations: int) -> void:
    var values := PackedInt64Array([1, 3, 5, 7, 9, 11, 13, 15])
    var total: int = checksum
    for value in range(iterations):
        var index := value & 7
        values[index] += value & 3
        total += values[index]
    checksum = total


func _vector_batch(iterations: int) -> void:
    var value := Vector2(1.25, -3.5)
    var total: float = 0.0
    for index in range(iterations):
        value.x += float(index & 3) * 0.125
        value.y -= 0.25
        total += value.x * value.x + value.y * value.y
    checksum += int(total) & 0x7fffffff


func _object_property_batch(iterations: int) -> void:
    position = Vector2.ZERO
    var total: int = checksum
    for value in range(iterations):
        position.x += 0.25
        position.y -= 0.125
        total += int(position.x + position.y) + (value & 1)
    checksum = total


func _string_batch(iterations: int) -> void:
    var value := "gdpp-runtime-matrix"
    var total: int = checksum
    for index in range(iterations):
        value = value.to_upper() if (index & 1) == 0 else value.to_lower()
        total += value.length()
    checksum = total


func _callable_batch(iterations: int) -> void:
    var operation := func(value: int) -> int: return value * 3 + 1
    var total: int = checksum
    for index in range(iterations):
        total += operation.call(index & 255)
    checksum = total


func _allocation_batch(iterations: int) -> void:
    var total: int = checksum
    for value in range(iterations):
        var values: Array[int] = [value, value + 1, value + 2, value + 3]
        total += values[0] + values[3]
    checksum = total


func _sample_case(name: String, samples: int, iterations: int) -> Array[int]:
    var result: Array[int] = []
    for sample in range(samples + 1):
        var started := Time.get_ticks_usec()
        match name:
            "numeric_typed":
                _numeric_batch(iterations)
            "array_typed":
                _array_batch(iterations)
            "dictionary":
                _dictionary_batch(iterations)
            "method_calls":
                _method_batch(iterations)
            "variant_ops":
                _variant_batch(iterations)
            "signal_emit":
                _signal_batch(iterations)
            "branch_typed":
                _branch_batch(iterations)
            "packed_array":
                _packed_array_batch(iterations)
            "builtin_vector":
                _vector_batch(iterations)
            "object_property":
                _object_property_batch(iterations)
            "string_ops":
                _string_batch(iterations)
            "callable":
                _callable_batch(iterations)
            "allocation":
                _allocation_batch(iterations)
        var elapsed := Time.get_ticks_usec() - started
        if sample > 0:
            result.append(elapsed)
    return result


func run_matrix(samples: int, iterations: int) -> Dictionary:
    var cases := {}
    for name in [
        "numeric_typed",
        "branch_typed",
        "array_typed",
        "packed_array",
        "dictionary",
        "builtin_vector",
        "object_property",
        "string_ops",
        "method_calls",
        "callable",
        "variant_ops",
        "signal_emit",
        "allocation",
    ]:
        cases[name] = {
            "iterations": iterations,
            "samples_us": _sample_case(name, samples, iterations),
        }
    return {
        "schema": 1,
        "oracle": run_oracle(),
        "performance": cases,
        "checksum": checksum,
    }


func run_frame_batch(iterations: int) -> int:
    _numeric_batch(iterations)
    _branch_batch(iterations)
    _vector_batch(iterations)
    return checksum
