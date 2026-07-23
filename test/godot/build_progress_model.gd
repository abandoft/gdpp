extends SceneTree

const BUILD_PROGRESS := preload("res://addons/gdpp/build_progress.gd")
const EPSILON := 0.000001


func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    var stages := PackedStringArray(["release"])
    var samples: Array[Dictionary] = [
        {"stage": "release", "phase": "scan", "completed": 0, "total": 1, "value": 0.0},
        {
            "stage": "release",
            "phase": "scan",
            "completed": 1,
            "total": 1,
            "value": 1.0 / 7.0,
        },
        {
            "stage": "release",
            "phase": "parse",
            "completed": 32,
            "total": 64,
            "value": 1.5 / 7.0,
        },
        {
            "stage": "release",
            "phase": "translate",
            "completed": 32,
            "total": 64,
            "value": 3.5 / 7.0,
        },
        {
            "stage": "release",
            "phase": "compile",
            "completed": 32,
            "total": 64,
            "value": 5.5 / 7.0,
        },
        {"stage": "release", "phase": "link", "completed": 1, "total": 1, "value": 1.0},
        {
            "stage": "release",
            "phase": "complete",
            "completed": 1,
            "total": 1,
            "value": 1.0,
        },
    ]

    var previous := -1.0
    for sample in samples:
        var actual: float = BUILD_PROGRESS.calculate_hierarchical_progress(
            stages,
            sample.stage,
            sample.phase,
            sample.completed,
            sample.total
        )
        if absf(actual - float(sample.value)) > EPSILON:
            push_error("GDPP hierarchical progress mismatch: %s, actual=%f" % [sample, actual])
            quit(1)
            return
        if actual + EPSILON < previous:
            push_error("GDPP hierarchical progress moved backwards: %f -> %f" % [previous, actual])
            quit(1)
            return
        previous = actual

    var compile_text := BUILD_PROGRESS.format_task_text("compile", 3, 32)
    if compile_text != "Compiling project sources (3/32)":
        push_error("GDPP compile task counter mismatch: %s" % compile_text)
        quit(1)
        return
    if BUILD_PROGRESS.format_task_text("parse", 3, 32) != "Parsing GDScript files":
        push_error("GDPP non-compile task unexpectedly displayed a file counter")
        quit(1)
        return

    var immediate_label := BUILD_PROGRESS.ImmediateTaskLabel.new()
    get_root().add_child(immediate_label)
    immediate_label.configure(
        Vector2(424.0, 22.0),
        ThemeDB.fallback_font,
        ThemeDB.fallback_font_size,
        Color.WHITE
    )
    immediate_label.set_text_immediate(compile_text)
    immediate_label.queue_free()

    print("GDPP_BUILD_PROGRESS_MODEL_OK")
    quit(0)
