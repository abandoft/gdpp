extends "res://vendor_grandchild.gd"

const DEFERRED_PHYSICS_SCENE := preload("res://deferred_shape.tscn")
const INNER_DATA := preload("res://inner_data.gd")
const NETWORK_IMAGE := preload("res://network_image.gd")
const RUNTIME_SHADER := preload("res://runtime_shader.gdshader")

var _network_server: TCPServer
var _network_peer: StreamPeerTCP
var _network_png: PackedByteArray
var _network_sprite: Sprite2D
var _network_deadline_msec := 0
var _network_response_sent := false
var _shader_rect: TextureRect
var _shader_material: ShaderMaterial
var _shader_process_ticks := 0


func _ready() -> void:
    super._ready()
    call_deferred(&"_verify_export_runtime")


func _verify_export_runtime() -> void:
    if not is_class(&"VendorBase"):
        _fail("export changed the provider-owned Node type")
        return
    if compute(3) != 62 or invoke_hook(4) != 54:
        _fail("export changed attached inheritance or native dispatch")
        return
    if get_ready_notifications() != 1:
        _fail("export did not preserve the provider lifecycle callback")
        return

    var data := load("res://vendor_data.tres")
    if data == null or not data.is_class(&"VendorResource"):
        _fail("export changed the provider-owned Resource type")
        return
    if data.compute(2) != 62:
        _fail("export changed attached Resource fields or native dispatch")
        return

    var physics_fixture := DEFERRED_PHYSICS_SCENE.instantiate()
    if physics_fixture == null:
        _fail("deferred attached-script preload did not materialize at runtime")
        return
    var collision_shape := physics_fixture.get_node("CollisionShape2D") as CollisionShape2D
    if collision_shape == null or not collision_shape.shape is CircleShape2D:
        _fail("deferred attached-script preload lost its physics resource")
        return
    physics_fixture.queue_free()

    var reflected_constants: Dictionary = get_script().get_script_constant_map()
    if reflected_constants.get(&"DEFERRED_PHYSICS_SCENE") != DEFERRED_PHYSICS_SCENE:
        _fail("attached Script reflection did not materialize a local deferred constant")
        return
    if reflected_constants.get(&"INHERITED_PHYSICS_SCENE") != INHERITED_PHYSICS_SCENE:
        _fail("attached Script reflection did not inherit a deferred constant")
        return

    var dynamic_entry: Variant = INNER_DATA.Entry.new()
    dynamic_entry.count = 42
    dynamic_entry.label = "attached"
    var indexed_entries: Dictionary[int, Variant] = {}
    indexed_entries[dynamic_entry.count] = dynamic_entry
    if (
        dynamic_entry.count != 42
        or dynamic_entry.label != "attached"
        or indexed_entries.get(42) != dynamic_entry
    ):
        _fail("dynamic attached inner-class properties lost typed getter/setter semantics")
        return

    _shader_rect = TextureRect.new()
    add_child(_shader_rect)
    _shader_material = ShaderMaterial.new()
    _shader_material.shader = RUNTIME_SHADER
    _shader_material.set_shader_parameter(&"pulse", 0.25)
    _shader_rect.material = _shader_material
    if (
        _shader_rect.material != _shader_material
        or not _shader_rect.material is ShaderMaterial
        or not is_equal_approx(
            float((_shader_rect.material as ShaderMaterial).get_shader_parameter(&"pulse")),
            0.25,
        )
    ):
        _fail("derived ShaderMaterial was not assigned through the Material property ABI")
        return

    var image := Image.create(2, 2, false, Image.FORMAT_RGBA8)
    image.fill(Color(0.25, 0.5, 0.75, 1.0))
    _network_png = image.save_png_to_buffer()
    _network_server = TCPServer.new()
    if _network_server.listen(0, "127.0.0.1") != OK:
        _fail("loopback HTTP fixture could not listen")
        return
    _network_sprite = Sprite2D.new()
    add_child(_network_sprite)
    var url := "http://127.0.0.1:%d/image.png" % _network_server.get_local_port()
    if NETWORK_IMAGE.load_into_sprite(url, _network_sprite) != OK:
        _fail("HTTPRequest rejected the loopback image URL")
        return
    _network_deadline_msec = Time.get_ticks_msec() + 5000


func _process(_delta: float) -> void:
    if _shader_material != null:
        _shader_process_ticks += 1
        _shader_material.set_shader_parameter(&"pulse", float(_shader_process_ticks))
    if _network_server == null:
        return
    if _network_peer == null and _network_server.is_connection_available():
        _network_peer = _network_server.take_connection()
    if _network_peer != null and not _network_response_sent:
        _network_peer.poll()
        var available := _network_peer.get_available_bytes()
        if available > 0:
            var request_text := _network_peer.get_utf8_string(available)
            if not request_text.begins_with("GET /image.png "):
                _fail("loopback HTTP request path was corrupted")
                return
            var response_headers := (
                "HTTP/1.1 200 OK\r\n"
                + "Content-Type: image/png\r\n"
                + "Content-Length: %d\r\n" % _network_png.size()
                + "Connection: close\r\n\r\n"
            )
            if (
                _network_peer.put_data(response_headers.to_utf8_buffer()) != OK
                or _network_peer.put_data(_network_png) != OK
            ):
                _fail("loopback HTTP response could not be written")
                return
            _network_response_sent = true
            _network_peer.disconnect_from_host()
            _network_server.stop()
    if _network_sprite != null and _network_sprite.has_meta(&"gdpp_network_error"):
        _fail(str(_network_sprite.get_meta(&"gdpp_network_error")))
        return
    if _network_sprite != null and _network_sprite.has_meta(&"gdpp_network_loaded"):
        var texture := _network_sprite.texture
        if (
            texture == null
            or not texture is ImageTexture
            or texture.get_width() != 2
            or texture.get_height() != 2
            or _shader_process_ticks <= 0
            or not is_equal_approx(
                float(_shader_material.get_shader_parameter(&"pulse")),
                float(_shader_process_ticks),
            )
        ):
            _fail("network ImageTexture or per-frame shader parameter state was not preserved")
            return
        print("GDPP_ATTACHED_EXPORT_RUNTIME_OK")
        get_tree().quit(0)
        return
    if Time.get_ticks_msec() >= _network_deadline_msec:
        _fail("loopback network image request timed out")


func _fail(message: String) -> void:
    if _network_server != null:
        _network_server.stop()
    push_error("GDPP attached export runtime: %s" % message)
    get_tree().quit(1)
