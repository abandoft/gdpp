extends Node
class_name NativeProjectPlayer

var assertion_evaluations: int = 0


func answer() -> int:
    return 40 + 2


func assert_answer(value: int) -> int:
    assert(value == 42, "native assertion value must be 42")
    return value


func assertion_evaluation_count() -> int:
    assertion_evaluations = 0
    assert(_record_assertion_evaluation())
    return assertion_evaluations


func _record_assertion_evaluation() -> bool:
    assertion_evaluations += 1
    return true
