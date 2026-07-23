extends RefCounted


class PacketParserBase:
    func parse_from_bytes(value: PackedByteArray) -> PackedByteArray:
        value.append(0xa1)
        return value


class PacketParser extends PacketParserBase:
    var suffix: PackedByteArray

    func _init(value: PackedByteArray) -> void:
        suffix = value

    func parse_variant(value: Variant) -> PackedByteArray:
        var typed: PackedByteArray = value
        typed.append_array(suffix)
        return super.parse_from_bytes(typed)


func encode_varint(output: PackedByteArray, value: int) -> void:
    var remaining := value
    while remaining >= 0x80:
        output.append((remaining & 0x7f) | 0x80)
        remaining >>= 7
    output.append(remaining)


func encode_tag(output: PackedByteArray, field_number: int, wire_type: int) -> void:
    encode_varint(output, (field_number << 3) | wire_type)


func encode_string(output: PackedByteArray, field_number: int, value: String) -> void:
    var encoded := value.to_utf8_buffer()
    encode_tag(output, field_number, 2)
    encode_varint(output, encoded.size())
    output.append_array(encoded)


func encode_login(username: String, password: String) -> PackedByteArray:
    var body := PackedByteArray()
    encode_string(body, 1, username)
    encode_string(body, 2, password)
    return body


func packet() -> PackedByteArray:
    var body := encode_login("123123", "123123")
    var output := PackedByteArray()
    encode_varint(output, body.size())
    output.append_array(body)
    return output


func variant_boundary_packet() -> PackedByteArray:
    var dynamic_body: Variant = encode_login("123123", "123123")
    var dynamic_suffix: Variant = PackedByteArray([0xa0])
    var parser := PacketParser.new(dynamic_suffix)
    parser.parse_from_bytes(dynamic_body)
    return parser.parse_variant(dynamic_body)
