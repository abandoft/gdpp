extends RefCounted
class_name OptimizationCase

var trace: Array = []


func run() -> Array:
    trace.clear()
    if 20 + 22 == 42:
        trace.append("live")
    else:
        trace.append("dead-branch")
    while false:
        trace.append("dead-loop")
    return trace.duplicate()
