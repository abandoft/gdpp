extends RefCounted

signal selected(value: Variant)

var trace: Array = []
var values: Array = []
var control_trace: Array = []
var control_result: Array = []
var parameter_result: int = -1
var match_trace: Array = []
var match_result: int = -1
var assert_trace: Array = []
var coroutine_values: Array = []
var awaited_property: int = 0:
    set(value):
        while await selected:
            value += 1
            if value == 1:
                continue
            break
        awaited_property = value


func _mark(value: int) -> int:
    trace.append(value)
    return value


func _control_bool(marker: int, value: bool) -> bool:
    control_trace.append(marker)
    return value


func _control_value(value: int) -> int:
    control_trace.append(value)
    return value


func _match_selector(value: int) -> int:
    match_trace.append(100 + value)
    return value


func _match_guard(marker: int) -> bool:
    match_trace.append(marker)
    return true


func run() -> void:
    var total: int = _mark(10) + await selected
    trace.append(total)
    values = [_mark(20), await selected, _mark(30), total]
    if await selected:
        trace.append(40)


func run_control_flow() -> void:
    control_trace = []
    control_result = []
    var conjunction: bool = _control_bool(1, true) and await selected
    control_trace.append(10 if conjunction else -10)
    var skipped_conjunction: bool = _control_bool(0, false) and await selected
    control_trace.append(20 if skipped_conjunction else -20)
    var skipped_disjunction: bool = _control_bool(2, true) or await selected
    control_trace.append(20 if skipped_disjunction else -20)
    var false_branch = (await selected) if await selected else _control_value(30)
    control_trace.append(false_branch)
    var true_branch = (await selected) if await selected else _control_value(31)
    control_trace.append(true_branch)
    var count: int = 0
    while await selected:
        count += 1
        control_trace.append(100 + count)
        if count == 1:
            continue
        if count == 2:
            break
    control_result = [
        conjunction,
        skipped_conjunction,
        skipped_disjunction,
        false_branch,
        true_branch,
        count,
    ]


func run_parameter_loop(count: int) -> void:
    parameter_result = -1
    while await selected:
        count += 1
        if count == 1:
            continue
        break
    parameter_result = count


func run_match(value: int) -> void:
    match_trace = []
    match_result = -1
    match _match_selector(value):
        var captured when _match_guard(captured * 10) and await selected:
            match_trace.append(captured)
            await selected
            match_result = captured
        2 when _match_guard(20):
            match_trace.append(200)
            match_result = 20
        _:
            match_trace.append(300)
            match_result = 30
    match_trace.append(999)


func immediate_match(value: int) -> int:
    match value:
        1 when await true:
            return 10
        _:
            return 20


func run_assert_success() -> void:
    assert_trace = []
    assert(await selected, str(await selected))
    assert_trace.append(1)


func run_assert_failure() -> void:
    assert_trace = []
    assert(await selected, str(await selected))
    assert_trace.append(2)


func reset_coroutine_values() -> void:
    coroutine_values = []


func produce_after_signal(value: int) -> int:
    await selected
    return value + 1


func produce_void_after_signal() -> void:
    await selected


func produce_maybe_immediate(immediate: bool) -> int:
    if immediate:
        return 11
    await selected
    return 12


func consume_produced(value: int) -> void:
    coroutine_values.append(await produce_after_signal(value))


func consume_void() -> void:
    coroutine_values.append(await produce_void_after_signal())


func consume_maybe_immediate(immediate: bool) -> void:
    coroutine_values.append(await produce_maybe_immediate(immediate))
