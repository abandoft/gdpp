@tool
extends CanvasLayer

const PANEL_SIZE := Vector2(480.0, 112.0)
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
const KNOWN_STAGES := {
    "development": true,
    "debug": true,
    "release": true,
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

class ImmediateProgressFill:
    extends Control

    var _fraction := 0.0
    var _color := Color.WHITE

    func configure(extent: Vector2, color: Color) -> void:
        size = extent
        _color = color
        set_progress_immediate(0.0)

    func set_progress_immediate(fraction: float) -> void:
        _fraction = clampf(fraction, 0.0, 1.0)
        queue_redraw()
        _submit_draw_commands()

    func _draw() -> void:
        var rect := _fill_rect()
        if rect.size.x > 0.0:
            draw_rect(rect, _color)

    func _submit_draw_commands() -> void:
        var canvas_item := get_canvas_item()
        RenderingServer.canvas_item_clear(canvas_item)
        var rect := _fill_rect()
        if rect.size.x > 0.0:
            RenderingServer.canvas_item_add_rect(canvas_item, rect, _color)

    func _fill_rect() -> Rect2:
        return Rect2(Vector2.ZERO, Vector2(size.x * _fraction, size.y))


class ImmediateTaskLabel:
    extends Control

    var _text := ""
    var _font: Font
    var _font_size := 13
    var _color := Color.WHITE

    func configure(extent: Vector2, font: Font, font_size: int, color: Color) -> void:
        size = extent
        _font = font
        _font_size = font_size
        _color = color
        set_text_immediate("")

    func set_text_immediate(text: String) -> void:
        if text == _text:
            return
        _text = text
        queue_redraw()
        _submit_draw_commands()

    func _draw() -> void:
        _draw_text()

    func _submit_draw_commands() -> void:
        RenderingServer.canvas_item_clear(get_canvas_item())
        _draw_text()

    func _draw_text() -> void:
        if _font == null or _text.is_empty():
            return
        var baseline := (
            _font.get_ascent(_font_size)
            + (size.y - _font.get_height(_font_size)) * 0.5
        )
        _font.draw_string(
            get_canvas_item(),
            Vector2(0.0, baseline),
            _text,
            HORIZONTAL_ALIGNMENT_LEFT,
            size.x,
            _font_size,
            _color
        )


var _surface: Control
var _panel_group: Control
var _track: ColorRect
var _fill: ImmediateProgressFill
var _task_label: ImmediateTaskLabel
var _stages := PackedStringArray()
var _target_progress := 0.0
var _displayed_progress := 0.0


func _ready() -> void:
    if DisplayServer.get_name().to_lower() == "headless":
        return

    layer = 127
    var editor_scale := EditorInterface.get_editor_scale()
    var panel_size := PANEL_SIZE * editor_scale
    var window_margin := WINDOW_MARGIN * editor_scale
    var group_size := panel_size + window_margin * 2.0

    _surface = Control.new()
    _surface.name = "GDPPNativeBuildProgress"
    _surface.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _surface.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    _surface.modulate = Color(1.0, 1.0, 1.0, 0.0)
    add_child(_surface)

    _panel_group = Control.new()
    _panel_group.size = group_size
    _surface.add_child(_panel_group)

    var shadow := ColorRect.new()
    shadow.position = Vector2(0.0, 5.0) * editor_scale
    shadow.size = group_size
    shadow.color = Color(0.0, 0.0, 0.0, 0.34)
    _panel_group.add_child(shadow)

    var panel := ColorRect.new()
    panel.position = window_margin
    panel.size = panel_size
    panel.color = PANEL_COLOR
    _panel_group.add_child(panel)

    var title_label := _make_label(
        "GDPP AOT Build",
        Vector2(28.0, 16.0) * editor_scale,
        Vector2(424.0, 28.0) * editor_scale,
        int(round(18.0 * editor_scale))
    )
    panel.add_child(title_label)

    _task_label = ImmediateTaskLabel.new()
    _task_label.position = Vector2(28.0, 47.0) * editor_scale
    panel.add_child(_task_label)
    _task_label.configure(
        Vector2(424.0, 22.0) * editor_scale,
        title_label.get_theme_font("font"),
        int(round(13.0 * editor_scale)),
        Color(0.94, 0.95, 0.98, 1.0)
    )

    _track = ColorRect.new()
    _track.position = Vector2(28.0, 78.0) * editor_scale
    _track.size = Vector2(424.0, 12.0) * editor_scale
    _track.color = TRACK_COLOR
    _track.clip_contents = true
    panel.add_child(_track)

    _fill = ImmediateProgressFill.new()
    _fill.position = Vector2.ZERO
    _track.add_child(_fill)
    _fill.configure(_track.size, FILL_COLOR)

    _center_on_window(EditorInterface.get_base_control().get_window())


func is_available() -> bool:
    return (
        _surface != null
        and _panel_group != null
        and _track != null
        and _fill != null
        and _task_label != null
    )


func begin(stages: PackedStringArray) -> void:
    if not is_available():
        return
    _stages = _normalized_stages(stages)
    _target_progress = 0.0
    _displayed_progress = 0.0
    _set_phase("scan")
    _set_progress(0.0)
    refresh(true)
    _attach_to_frontmost_editor_window()
    _surface.modulate = Color.WHITE
    _redraw_now()


func set_active_stage(stage: String) -> void:
    if not is_available():
        return
    _set_phase("scan")
    _set_progress(calculate_hierarchical_progress(_stages, stage, "scan", 0, 1))
    _redraw_now()


func update(stage: String, phase: String, completed: int, total: int) -> void:
    if not is_available():
        return
    var known_phase := phase if PHASE_TEXT.has(phase) else "compile"
    _set_phase(known_phase, completed, total)
    _set_progress(
        calculate_hierarchical_progress(_stages, stage, known_phase, completed, total)
    )
    _redraw_now()


func finish() -> void:
    if not is_available():
        return
    _set_phase("complete")
    _set_progress(1.0)
    refresh(true)
    _redraw_now()
    _surface.modulate = Color(1.0, 1.0, 1.0, 0.0)
    _redraw_now()
    _restore_editor_parent()


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


static func format_task_text(phase: String, completed: int, total: int) -> String:
    var known_phase := phase if PHASE_TEXT.has(phase) else "compile"
    var text: String = PHASE_TEXT[known_phase]
    if known_phase == "compile" and total > 0:
        text += " %d/%d" % [clampi(completed, 0, total), total]
    return text


func _normalized_stages(stages: PackedStringArray) -> PackedStringArray:
    var normalized := PackedStringArray()
    for stage in stages:
        if KNOWN_STAGES.has(stage) and not normalized.has(stage):
            normalized.push_back(stage)
    if normalized.is_empty():
        normalized.push_back("development")
    return normalized


func _set_phase(phase: String, completed := 0, total := 0) -> void:
    var known_phase := phase if PHASE_TEXT.has(phase) else "compile"
    _task_label.set_text_immediate(format_task_text(known_phase, completed, total))


func _set_progress(progress: float) -> void:
    _target_progress = maxf(_target_progress, clampf(progress, 0.0, 1.0))


func refresh(snap_to_target := false) -> void:
    if not is_available():
        return
    if snap_to_target:
        _displayed_progress = _target_progress
    else:
        var remaining := _target_progress - _displayed_progress
        if remaining > 0.0:
            _displayed_progress = minf(
                _target_progress,
                _displayed_progress + maxf(0.0015, remaining * 0.16)
            )
    # The export callback owns the editor loop, so queue_redraw() alone cannot
    # deliver CanvasItem draw notifications. Submit the current rectangle
    # directly to RenderingServer; _draw() keeps normal editor frames in sync.
    _fill.set_progress_immediate(_displayed_progress)
    _redraw_now()


func _attach_to_frontmost_editor_window() -> void:
    var host_window := _frontmost_editor_window()
    if host_window == null:
        return
    if get_parent() != host_window:
        reparent(host_window, false)
    _center_on_window(host_window)


func _frontmost_editor_window() -> Window:
    var editor_window := EditorInterface.get_base_control().get_window()
    if editor_window == null:
        return null
    var exclusive_window := editor_window.get_last_exclusive_window()
    if exclusive_window != null and exclusive_window.visible:
        return exclusive_window
    var popup_window := _find_window_by_id(
        editor_window,
        DisplayServer.window_get_active_popup()
    )
    return popup_window if popup_window != null else editor_window


func _find_window_by_id(node: Node, window_id: int) -> Window:
    if node is Window:
        var candidate := node as Window
        if candidate.get_window_id() == window_id:
            return candidate
    for child in node.get_children():
        var result := _find_window_by_id(child, window_id)
        if result != null:
            return result
    return null


func _center_on_window(host_window: Window) -> void:
    if host_window == null:
        return
    _panel_group.position = (
        Vector2(host_window.size) - _panel_group.size
    ) * 0.5


func _restore_editor_parent() -> void:
    var editor_parent := EditorInterface.get_base_control()
    if editor_parent != null and get_parent() != editor_parent:
        reparent(editor_parent, false)
    if editor_parent != null:
        _center_on_window(editor_parent.get_window())


func _make_label(text: String, position: Vector2, size: Vector2, font_size: int) -> Label:
    var label := Label.new()
    label.text = text
    label.position = position
    label.size = size
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", font_size)
    return label


func _redraw_now() -> void:
    DisplayServer.process_events()
    RenderingServer.force_sync()
    RenderingServer.force_draw(true)
