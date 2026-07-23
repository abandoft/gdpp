extends RefCounted


signal packed_emitted(values: PackedByteArray)


var default_bytes := PackedByteArray([4])


func mutate_bytes(values: PackedByteArray) -> void:
    values.append(2)


func mutate_int32(values: PackedInt32Array) -> void:
    values.append(12)


func mutate_int64(values: PackedInt64Array) -> void:
    values.append(22)


func mutate_float32(values: PackedFloat32Array) -> void:
    values.append(1.25)


func mutate_float64(values: PackedFloat64Array) -> void:
    values.append(2.5)


func mutate_strings(values: PackedStringArray) -> void:
    values.append("beta")


func mutate_vector2(values: PackedVector2Array) -> void:
    values.append(Vector2(3.0, 4.0))


func mutate_vector3(values: PackedVector3Array) -> void:
    values.append(Vector3(4.0, 5.0, 6.0))


func mutate_vector4(values: PackedVector4Array) -> void:
    values.append(Vector4(5.0, 6.0, 7.0, 8.0))


func mutate_colors(values: PackedColorArray) -> void:
    values.append(Color(0.25, 0.5, 0.75, 1.0))


func identity_bytes(values: PackedByteArray) -> PackedByteArray:
    return values


func rebind_bytes(values: PackedByteArray) -> void:
    values = PackedByteArray([90])
    values.append(91)


func mutate_default(values: PackedByteArray = default_bytes) -> PackedByteArray:
    values.append(5)
    return values


func callable_bytes(values: PackedByteArray) -> PackedByteArray:
    var mutate := func(target: PackedByteArray) -> void:
        target.append(7)
    mutate.call(values)
    packed_emitted.connect(
        func(target: PackedByteArray) -> void:
            target.append(8),
        CONNECT_ONE_SHOT,
    )
    packed_emitted.emit(values)
    var dynamic_self: Variant = self
    dynamic_self.call("mutate_bytes", values)
    return values


func run() -> Dictionary:
    var bytes := PackedByteArray([1])
    var bytes_alias := bytes
    mutate_bytes(bytes)
    bytes_alias[0] = 9

    var int32 := PackedInt32Array([11])
    var int32_alias := int32
    mutate_int32(int32)

    var int64 := PackedInt64Array([21])
    var int64_alias := int64
    mutate_int64(int64)

    var float32 := PackedFloat32Array([0.5])
    var float32_alias := float32
    mutate_float32(float32)

    var float64 := PackedFloat64Array([1.5])
    var float64_alias := float64
    mutate_float64(float64)

    var strings := PackedStringArray(["alpha"])
    var strings_alias := strings
    mutate_strings(strings)

    var vector2 := PackedVector2Array([Vector2(1.0, 2.0)])
    var vector2_alias := vector2
    mutate_vector2(vector2)

    var vector3 := PackedVector3Array([Vector3(1.0, 2.0, 3.0)])
    var vector3_alias := vector3
    mutate_vector3(vector3)

    var vector4 := PackedVector4Array([Vector4(1.0, 2.0, 3.0, 4.0)])
    var vector4_alias := vector4
    mutate_vector4(vector4)

    var colors := PackedColorArray([Color(1.0, 0.0, 0.0, 1.0)])
    var colors_alias := colors
    mutate_colors(colors)

    var returned := identity_bytes(bytes)
    returned.append(3)
    var rebound := bytes
    rebind_bytes(rebound)
    var explicit_copy := PackedByteArray(bytes)
    explicit_copy.append(99)
    var callable_result := callable_bytes(bytes)
    var default_result := mutate_default()

    return {
        "bytes": [bytes, bytes_alias, returned, callable_result],
        "int32": [int32, int32_alias],
        "int64": [int64, int64_alias],
        "float32": [float32, float32_alias],
        "float64": [float64, float64_alias],
        "strings": [strings, strings_alias],
        "vector2": [vector2, vector2_alias],
        "vector3": [vector3, vector3_alias],
        "vector4": [vector4, vector4_alias],
        "colors": [colors, colors_alias],
        "rebind": rebound,
        "copy": explicit_copy,
        "default": [default_bytes, default_result],
    }
