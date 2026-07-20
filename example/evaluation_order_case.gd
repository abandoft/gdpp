extends Resource

var trace: Array[int] = []


func mark_int(identifier: int, value: int) -> int:
    trace.append(identifier)
    return value


func mark_float(identifier: int, value: float) -> float:
    trace.append(identifier)
    return value


func mark_string(identifier: int, value: String) -> String:
    trace.append(identifier)
    return value


func mark_vector(identifier: int, value: Vector2) -> Vector2:
    trace.append(identifier)
    return value


func mark_variant(identifier: int, value: Variant) -> Variant:
    trace.append(identifier)
    return value


func run() -> Dictionary:
    trace.clear()
    var arithmetic := mark_float(1, 1.5) + mark_float(2, 2.5)
    var text := mark_string(3, "left") + mark_string(4, "right")
    var dynamic: Variant = mark_variant(5, 20) + mark_variant(6, 22)
    var vector := mark_vector(7, Vector2(1, 2)) + mark_vector(8, Vector2(3, 4))
    var membership: bool = mark_variant(9, "x") in mark_variant(10, ["x"])
    var power := mark_float(11, 2.0) ** mark_float(12, 3.0)
    var comparison := mark_float(13, 1.0) < mark_float(14, 2.0)
    var integer := mark_int(15, 20) + mark_int(16, 22)
    var chained: bool = mark_int(17, 1) == mark_int(18, 1) == mark_variant(19, true)
    return {
        "trace": trace.duplicate(),
        "arithmetic": arithmetic,
        "text": text,
        "dynamic": dynamic,
        "vector": vector,
        "membership": membership,
        "power": power,
        "comparison": comparison,
        "integer": integer,
        "chained": chained,
    }
