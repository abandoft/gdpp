extends Node

static var _pending: Dictionary = {}


static func load_into_sprite(url: String, sprite: Sprite2D) -> Error:
    var request := HTTPRequest.new()
    sprite.add_child(request)
    _pending[sprite] = request
    request.request_completed.connect(
        _on_request_completed.bind(request, sprite),
        CONNECT_ONE_SHOT,
    )
    var error := request.request(url, ["User-Agent: GDPP integration"], HTTPClient.METHOD_GET)
    if error != OK:
        _pending.erase(sprite)
        request.queue_free()
    return error


static func _on_request_completed(
    result: int,
    response_code: int,
    headers: PackedStringArray,
    body: PackedByteArray,
    request: HTTPRequest,
    sprite: Sprite2D,
) -> void:
    if not _pending.has(sprite) or _pending[sprite] != request:
        sprite.set_meta(&"gdpp_network_error", "request identity was not preserved")
        request.queue_free()
        return
    _pending.erase(sprite)
    request.queue_free()
    if result != HTTPRequest.RESULT_SUCCESS or response_code != 200:
        sprite.set_meta(
            &"gdpp_network_error",
            "request failed: result=%d response=%d" % [result, response_code],
        )
        return
    var content_type := ""
    for header in headers:
        if header.to_lower().begins_with("content-type:"):
            content_type = header.get_slice(":", 1).strip_edges().to_lower()
            break
    if not content_type.contains("image/png"):
        sprite.set_meta(&"gdpp_network_error", "response headers lost the PNG content type")
        return
    var image := Image.new()
    var decode_error := image.load_png_from_buffer(body)
    if decode_error != OK:
        sprite.set_meta(&"gdpp_network_error", "PNG response could not be decoded")
        return
    var texture: Texture2D = ImageTexture.create_from_image(image)
    sprite.texture = texture
    sprite.set_meta(&"gdpp_network_loaded", true)
