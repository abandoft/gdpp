@tool
extends CanvasLayer

const PANEL_SIZE := Vector2(480.0, 136.0)
const PANEL_COLOR := Color(0.075, 0.086, 0.11, 0.98)
const TRACK_COLOR := Color(0.18, 0.2, 0.24, 1.0)
const FILL_COLOR := Color(0.24, 0.58, 0.96, 1.0)
const FILL_COLUMN_COUNT := 424

var _surface: Control
var _fill_columns: Array[ColorRect] = []
var _phase_labels: Dictionary = {}
var _profile_labels: Dictionary = {}
var _active_phase := ""
var _active_profile := ""
var _displayed_progress := 0.0


func _ready() -> void:
    if DisplayServer.get_name().to_lower() == "headless":
        return

    layer = 127
    var editor_scale := EditorInterface.get_editor_scale()
    var panel_size := PANEL_SIZE * editor_scale

    _surface = Control.new()
    _surface.name = "GDPPNativeBuildProgress"
    _surface.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _surface.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    _surface.modulate = Color(1.0, 1.0, 1.0, 0.0)
    add_child(_surface)

    var shadow := ColorRect.new()
    shadow.color = Color(0.0, 0.0, 0.0, 0.34)
    _place_centered(shadow, panel_size + Vector2(12.0, 12.0) * editor_scale)
    shadow.position += Vector2(0.0, 5.0) * editor_scale
    _surface.add_child(shadow)

    var panel := ColorRect.new()
    panel.color = PANEL_COLOR
    _place_centered(panel, panel_size)
    _surface.add_child(panel)

    var title := _make_label(
        "GDPP Native Build",
        Vector2(28.0, 16.0) * editor_scale,
        Vector2(424.0, 28.0) * editor_scale,
        int(round(18.0 * editor_scale))
    )
    panel.add_child(title)

    for profile in ["prepare", "development", "debug", "release"]:
        var profile_text: String = {
            "prepare": "Preparing binary-only export",
            "development": "Validating the editor-native bridge",
            "debug": "Building the debug export library",
            "release": "Building the release export library",
        }[profile]
        var profile_label := _make_label(
            profile_text,
            Vector2(28.0, 43.0) * editor_scale,
            Vector2(424.0, 22.0) * editor_scale,
            int(round(13.0 * editor_scale))
        )
        profile_label.modulate = Color(0.72, 0.76, 0.83, 0.0)
        panel.add_child(profile_label)
        _profile_labels[profile] = profile_label

    for phase in ["scan", "parse", "analyze", "translate", "generate", "compile", "link", "complete"]:
        var phase_text: String = {
            "scan": "Scanning project sources",
            "parse": "Parsing GDScript files",
            "analyze": "Analyzing project semantics",
            "translate": "Translating GDScript to C++",
            "generate": "Writing native build files",
            "compile": "Compiling C++ translation units",
            "link": "Linking the native library",
            "complete": "Native build complete",
        }[phase]
        var phase_label := _make_label(
            phase_text,
            Vector2(28.0, 67.0) * editor_scale,
            Vector2(424.0, 22.0) * editor_scale,
            int(round(13.0 * editor_scale))
        )
        phase_label.modulate = Color(0.94, 0.95, 0.98, 0.0)
        panel.add_child(phase_label)
        _phase_labels[phase] = phase_label

    var track := ColorRect.new()
    track.position = Vector2(28.0, 101.0) * editor_scale
    track.size = Vector2(424.0, 12.0) * editor_scale
    track.color = TRACK_COLOR
    track.clip_contents = true
    panel.add_child(track)

    var column_width := track.size.x / float(FILL_COLUMN_COUNT)
    for index in range(FILL_COLUMN_COUNT):
        var column := ColorRect.new()
        column.position = Vector2(float(index) * column_width, 0.0)
        column.size = Vector2(column_width + 0.5, track.size.y)
        column.color = FILL_COLOR
        column.modulate = Color(1.0, 1.0, 1.0, 0.0)
        track.add_child(column)
        _fill_columns.push_back(column)


func is_available() -> bool:
    return _surface != null and _fill_columns.size() == FILL_COLUMN_COUNT


func begin() -> void:
    if not is_available():
        return
    _displayed_progress = 0.0
    _set_profile("prepare")
    _set_phase("scan")
    _set_progress(0.0)
    _surface.modulate = Color.WHITE
    _redraw_now()


func set_translation_profile(profile: String) -> void:
    if not is_available():
        return
    _displayed_progress = 0.0
    _set_profile(profile)
    _set_phase("scan")
    _set_progress(0.0)
    _redraw_now()


func update(profile: String, phase: String, completed: int, total: int) -> void:
    if not is_available():
        return
    _set_profile(profile)
    var known_phase := phase if _phase_labels.has(phase) else "compile"
    _set_phase(known_phase)
    var phase_progress := (
        1.0 if total <= 0 else clampf(float(completed) / float(total), 0.0, 1.0)
    )
    _set_progress(_overall_progress(known_phase, phase_progress))
    _redraw_now()


func finish() -> void:
    if not is_available():
        return
    _set_phase("complete")
    _set_progress(1.0)
    _redraw_now()
    _surface.modulate = Color(1.0, 1.0, 1.0, 0.0)
    _redraw_now()


func _set_phase(phase: String) -> void:
    if phase == _active_phase:
        return
    if _phase_labels.has(_active_phase):
        _phase_labels[_active_phase].modulate = Color(0.94, 0.95, 0.98, 0.0)
    _phase_labels[phase].modulate = Color(0.94, 0.95, 0.98, 1.0)
    _active_phase = phase


func _set_profile(profile: String) -> void:
    if profile == _active_profile:
        return
    if not _profile_labels.has(profile):
        profile = "prepare"
    if _profile_labels.has(_active_profile):
        _profile_labels[_active_profile].modulate = Color(0.72, 0.76, 0.83, 0.0)
    _profile_labels[profile].modulate = Color(0.72, 0.76, 0.83, 1.0)
    _active_profile = profile


func _set_progress(progress: float) -> void:
    _displayed_progress = maxf(_displayed_progress, clampf(progress, 0.0, 1.0))
    var visible_columns := int(floor(_displayed_progress * float(FILL_COLUMN_COUNT)))
    for index in range(FILL_COLUMN_COUNT):
        var alpha := 1.0 if index < visible_columns else 0.0
        _fill_columns[index].modulate = Color(1.0, 1.0, 1.0, alpha)


func _overall_progress(phase: String, phase_progress: float) -> float:
    var phase_range := Vector2(0.58, 0.92)
    match phase:
        "scan":
            phase_range = Vector2(0.0, 0.04)
        "parse":
            phase_range = Vector2(0.04, 0.16)
        "analyze":
            phase_range = Vector2(0.16, 0.36)
        "translate":
            phase_range = Vector2(0.36, 0.54)
        "generate":
            phase_range = Vector2(0.54, 0.58)
        "compile":
            phase_range = Vector2(0.58, 0.92)
        "link":
            phase_range = Vector2(0.92, 0.99)
        "complete":
            return 1.0
    return lerpf(phase_range.x, phase_range.y, phase_progress)


func _place_centered(control: Control, control_size: Vector2) -> void:
    control.set_anchors_preset(Control.PRESET_CENTER)
    control.position = control_size * -0.5
    control.size = control_size


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
    RenderingServer.force_draw()
