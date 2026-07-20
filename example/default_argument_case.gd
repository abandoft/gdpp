extends RefCounted

var seed: int = 10
var sequence: int = 0


func collect(values: Array = []) -> Array:
    values.append(values.size())
    return values


func next_value() -> int:
    sequence += 1
    return sequence


func from_call(value: int = next_value()) -> int:
    return value


func from_field(value: int = seed) -> int:
    return value


func folded(value: float = sin(PI / 2.0), component: float = Vector2(3, 4).x) -> Array:
    return [value, component]


func run() -> Dictionary:
    var first: Array = collect()
    var second: Array = collect()
    first.append(99)
    var initial_field: int = from_field()
    seed = 20
    return {
        "containers": [first, second],
        "calls": [from_call(), from_call(), from_call(42)],
        "fields": [initial_field, from_field()],
        "constants": folded(),
    }
