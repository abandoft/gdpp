@tool
extends Window

const PANEL_SIZE := Vector2(480.0, 136.0)
const WINDOW_MARGIN := Vector2(6.0, 6.0)
const PANEL_COLOR := Color(0.075, 0.086, 0.11, 0.98)
const TRACK_COLOR := Color(0.18, 0.2, 0.24, 1.0)
const FILL_COLOR := Color(0.24, 0.58, 0.96, 1.0)
const PHASE_ORDER := [
    "scan",
    "parse",
    "analyze",
    "translate",
    "generate",
    "compile",
    "link",
]
const STAGE_TEXT := {
    "prepare": "Preparing AOT export",
    "development": "Validating the editor-native bridge",
    "debug": "Building the debug export",
    "release": "Building the release export",
}
const PHASE_TEXT := {
    "scan": "Scanning project sources",
    "parse": "Parsing GDScript files",
    "analyze": "Analyzing project semantics",
    "translate": "Precompiling project scripts",
    "generate": "Writing native build files",
    "compile": "Compiling project sources",
    "link": "Linking the native library",
    "complete": "AOT build complete",
}

var _surface: Control
var _fill: ColorRect
var _track: ColorRect
var _stage_label: Label
var _phase_label: Label
var _item_counter: Label
var _stages := PackedStringArray()
var _active_stage := ""
var _active_phase := ""
var _displayed_progress := 0.0


func _ready() -> void:
    if DisplayServer.get_name().to_lower() == "headless":
        return

    name = "GDPPNativeBuildProgress"
    title = "GDPP Native Build"
    unresizable = true
    borderless = true
    transient = true
    transient_to_focused = true
    exclusive = true
    visible = false
    close_requested.connect(_keep_open_during_build)

    var editor_scale := EditorInterface.get_editor_scale()
    var panel_size := PANEL_SIZE * editor_scale
    var window_margin := WINDOW_MARGIN * editor_scale
    var window_size := Vector2i(ceil(panel_size.x + window_margin.x * 2.0), ceil(
        panel_size.y + window_margin.y * 2.0
    ))
    size = window_size
    min_size = window_size

    _surface = Control.new()
    _surface.mouse_filter = Control.MOUSE_FILTER_STOP
    _surface.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    add_child(_surface)

    var shadow := ColorRect.new()
    shadow.position = Vector2(0.0, 5.0) * editor_scale
    shadow.size = Vector2(window_size)
    shadow.color = Color(0.0, 0.0, 0.0, 0.34)
    _surface.add_child(shadow)

    var panel := ColorRect.new()
    panel.position = window_margin
    panel.size = panel_size
    panel.color = PANEL_COLOR
    _surface.add_child(panel)

    var title_label := _make_label(
        "GDPP Native Build",
        Vector2(28.0, 16.0) * editor_scale,
        Vector2(424.0, 28.0) * editor_scale,
        int(round(18.0 * editor_scale))
    )
    panel.add_child(title_label)

    _stage_label = _make_label(
        STAGE_TEXT["prepare"],
        Vector2(28.0, 43.0) * editor_scale,
        Vector2(424.0, 22.0) * editor_scale,
        int(round(13.0 * editor_scale))
    )
    _stage_label.modulate = Color(0.72, 0.76, 0.83, 1.0)
    panel.add_child(_stage_label)

    _phase_label = _make_label(
        PHASE_TEXT["scan"],
        Vector2(28.0, 67.0) * editor_scale,
        Vector2(340.0, 22.0) * editor_scale,
        int(round(13.0 * editor_scale))
    )
    _phase_label.modulate = Color(0.94, 0.95, 0.98, 1.0)
    panel.add_child(_phase_label)

    _item_counter = _make_label(
        "",
        Vector2(368.0, 67.0) * editor_scale,
        Vector2(84.0, 22.0) * editor_scale,
        int(round(12.0 * editor_scale))
    )
    _item_counter.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
    _item_counter.modulate = Color(0.72, 0.76, 0.83, 1.0)
    panel.add_child(_item_counter)

    _track = ColorRect.new()
    _track.position = Vector2(28.0, 101.0) * editor_scale
    _track.size = Vector2(424.0, 12.0) * editor_scale
    _track.color = TRACK_COLOR
    _track.clip_contents = true
    panel.add_child(_track)

    _fill = ColorRect.new()
    _fill.position = Vector2.ZERO
    _fill.size = Vector2(0.0, _track.size.y)
    _fill.color = FILL_COLOR
    _track.add_child(_fill)


func is_available() -> bool:
    return (
        _surface != null
        and _fill != null
        and _stage_label != null
        and _phase_label != null
    )


func begin(stages: PackedStringArray) -> void:
    if not is_available():
        return
    _stages = _normalized_stages(stages)
    _active_stage = ""
    _active_phase = ""
    _displayed_progress = 0.0
    _set_stage("prepare")
    _set_phase("scan")
    _set_item_counter(0, 0)
    _set_progress(0.0)
    popup_centered(size)
    _redraw_now()


func set_active_stage(stage: String) -> void:
    if not is_available():
        return
    _set_stage(stage)
    _set_phase("scan")
    _set_item_counter(0, 0)
    _set_progress(calculate_hierarchical_progress(_stages, stage, "scan", 0, 1))
    _redraw_now()


func update(stage: String, phase: String, completed: int, total: int) -> void:
    if not is_available():
        return
    _set_stage(stage)
    var known_phase := phase if PHASE_TEXT.has(phase) else "compile"
    _set_phase(known_phase)
    _set_item_counter(completed, total)
    _set_progress(
        calculate_hierarchical_progress(_stages, stage, known_phase, completed, total)
    )
    _redraw_now()


func finish() -> void:
    if not is_available():
        return
    _set_phase("complete")
    _set_item_counter(0, 0)
    _set_progress(1.0)
    _redraw_now()
    hide()
    _redraw_now()


static func calculate_hierarchical_progress(
    stages: PackedStringArray,
    stage: String,
    phase: String,
    completed: int,
    total: int
) -> float:
    var stage_count := maxi(stages.size(), 1)
    var stage_index := stages.find(stage)
    if stage_index < 0:
        stage_index = 0
    if phase == "complete":
        return clampf(float(stage_index + 1) / float(stage_count), 0.0, 1.0)

    var phase_index := PHASE_ORDER.find(phase)
    if phase_index < 0:
        phase_index = PHASE_ORDER.find("compile")
    var item_progress := (
        1.0 if total <= 0 else clampf(float(completed) / float(total), 0.0, 1.0)
    )
    var stage_progress := (
        float(phase_index) + item_progress
    ) / float(PHASE_ORDER.size())
    return clampf(
        (float(stage_index) + stage_progress) / float(stage_count),
        0.0,
        1.0
    )


func _normalized_stages(stages: PackedStringArray) -> PackedStringArray:
    var normalized := PackedStringArray()
    for stage in stages:
        if STAGE_TEXT.has(stage) and stage != "prepare" and not normalized.has(stage):
            normalized.push_back(stage)
    if normalized.is_empty():
        normalized.push_back("development")
    return normalized


func _set_stage(stage: String) -> void:
    var known_stage := stage if STAGE_TEXT.has(stage) else "prepare"
    if known_stage == _active_stage:
        return
    _stage_label.text = STAGE_TEXT[known_stage]
    _active_stage = known_stage


func _set_phase(phase: String) -> void:
    var known_phase := phase if PHASE_TEXT.has(phase) else "compile"
    if known_phase == _active_phase:
        return
    _phase_label.text = PHASE_TEXT[known_phase]
    _active_phase = known_phase


func _set_item_counter(completed: int, total: int) -> void:
    _item_counter.text = (
        "%d / %d" % [clampi(completed, 0, total), total]
        if total > 1
        else ""
    )


func _set_progress(progress: float) -> void:
    _displayed_progress = maxf(_displayed_progress, clampf(progress, 0.0, 1.0))
    _fill.size = Vector2(_track.size.x * _displayed_progress, _track.size.y)


func _make_label(text: String, position: Vector2, size: Vector2, font_size: int) -> Label:
    var label := Label.new()
    label.text = text
    label.position = position
    label.size = size
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", font_size)
    return label


func _keep_open_during_build() -> void:
    pass


func _redraw_now() -> void:
    DisplayServer.process_events()
    RenderingServer.force_draw()
