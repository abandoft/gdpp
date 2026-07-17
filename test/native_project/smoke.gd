extends SceneTree


func _init() -> void:
    call_deferred("_run")


func _native_class_for(source_path: String) -> StringName:
    var manifest := FileAccess.get_file_as_string(
        "res://addons/gdpp/build/project/manifest.txt"
    )
    for line in manifest.split("\n", false):
        var fields := line.split('"')
        if fields.size() >= 8 and fields[1] == source_path:
            return StringName(fields[7])
    return &""


func _run() -> void:
    var player_class := _native_class_for("player.gd")
    var hello_class := _native_class_for("hello.gd")
    var inheritance_class := _native_class_for("inheritance_child.gd")
    var cross_a_class := _native_class_for("cross_ref_a.gd")
    var cross_b_class := _native_class_for("cross_ref_b.gd")
    var required_init_class := _native_class_for("required_initializer.gd")
    var long_await_class := _native_class_for("long_await_chain.gd")
    if (
        player_class.is_empty()
        or hello_class.is_empty()
        or inheritance_class.is_empty()
        or cross_a_class.is_empty()
        or cross_b_class.is_empty()
        or required_init_class.is_empty()
        or long_await_class.is_empty()
    ):
        push_error("Generated native class manifest is incomplete")
        quit(1)
        return
    var instance: Object = ClassDB.instantiate(player_class)
    if (
        instance == null
        or instance.call("answer") != 42
        or instance.call("assert_answer", 42) != 42
        or instance.call("assertion_evaluation_count") != 1
    ):
        push_error("Generated native project class returned an invalid result")
        if instance != null:
            instance.free()
        quit(1)
        return
    instance.free()

    var coroutine: Object = ClassDB.instantiate(long_await_class)
    if coroutine == null:
        push_error("Generated flat async state machine class is unavailable")
        quit(1)
        return
    coroutine.call("run", true)
    if coroutine.get("checkpoint") != 1:
        push_error("Flat async state machine did not stop at its first suspension")
        coroutine.free()
        quit(1)
        return
    for expected_checkpoint in range(2, 12):
        coroutine.emit_signal("resumed")
        if coroutine.get("checkpoint") != expected_checkpoint:
            push_error(
                "Flat async state machine resumed out of order at checkpoint %d"
                % expected_checkpoint
            )
            coroutine.free()
            quit(1)
            return
    coroutine.free()

    var inherited: Object = ClassDB.instantiate(inheritance_class)
    if (
        inherited == null
        or inherited.call("inherited_answer") != 42
        or inherited.call("child_answer") != 42
        or inherited.call("read_base", inherited) != 42
        or inherited.call("typed_identity", inherited) != inherited
        or inherited.call("cross_constants") != 21
        or inherited.call("enum_roundtrip", 8) != 8
        or inherited.call("resource_factory_answer") != 45
        or inherited.call("required_init_answer") != 7
    ):
        push_error("Generated cross-script inheritance returned an invalid result")
        if inherited != null:
            inherited.free()
        quit(1)
        return
    var inherited_properties: Dictionary = {}
    for property: Dictionary in inherited.get_property_list():
        inherited_properties[str(property.get("name", ""))] = property
    if (
        not inherited_properties.has("shared_mode")
        or int(inherited_properties.shared_mode.get("hint", -1)) != PROPERTY_HINT_ENUM
        or str(inherited_properties.shared_mode.get("hint_string", ""))
        != "IDLE:0,ACTIVE:4,BOOST:8"
    ):
        push_error("Cross-script enum Inspector metadata is invalid")
        inherited.free()
        quit(1)
        return
    inherited.free()

    var default_initialized: Object = ClassDB.instantiate(required_init_class)
    if default_initialized == null or default_initialized.call("answer") != 0:
        push_error("Required _init script lost its ClassDB-compatible default constructor")
        if default_initialized != null:
            default_initialized.free()
        quit(1)
        return
    default_initialized.free()

    var cross_a: Object = ClassDB.instantiate(cross_a_class)
    var cross_b: Object = ClassDB.instantiate(cross_b_class)
    if (
        cross_a == null
        or cross_b == null
        or cross_a.call("connect_peer", cross_b) != cross_b
        or cross_b.call("connect_peer", cross_a) != cross_a
    ):
        push_error("Generated mutually typed script references are invalid")
        if cross_a != null:
            cross_a.free()
        if cross_b != null:
            cross_b.free()
        quit(1)
        return
    cross_a.free()
    cross_b.free()

    var exported: Object = ClassDB.instantiate(hello_class)
    if exported == null:
        push_error("Generated exported-property class is unavailable")
        quit(1)
        return
    var enum_constants := ClassDB.class_get_enum_constants(
        hello_class,
        &"MovementMode",
        true
    )
    if (
        not enum_constants.has("IDLE")
        or not enum_constants.has("WALK")
        or not enum_constants.has("RUN")
        or ClassDB.class_get_integer_constant(hello_class, &"RUN") != 8
        or ClassDB.class_get_integer_constant(hello_class, &"MAX_LIVES") != 5
    ):
        push_error("Generated enum constants were not registered in ClassDB")
        exported.free()
        quit(1)
        return
    var property_by_name: Dictionary = {}
    for property: Dictionary in exported.get_property_list():
        property_by_name[str(property.get("name", ""))] = property
    for required in [
        "greeting", "movement_speed", "accent", "icon", "movement_mode", "accessor_score"
    ]:
        if not property_by_name.has(required):
            push_error("Generated native class is missing exported property '%s'" % required)
            exported.free()
            quit(1)
            return
    var speed_info: Dictionary = property_by_name.movement_speed
    if int(speed_info.get("hint", -1)) != PROPERTY_HINT_RANGE:
        push_error("Generated movement_speed property lost its range hint")
        exported.free()
        quit(1)
        return
    if str(speed_info.get("hint_string", "")) != "0.0,100.0,0.5,or_greater":
        push_error("Generated movement_speed property has an invalid hint string")
        exported.free()
        quit(1)
        return
    var icon_info: Dictionary = property_by_name.icon
    if (
        int(icon_info.get("hint", -1)) != PROPERTY_HINT_RESOURCE_TYPE
        or str(icon_info.get("hint_string", "")) != "Texture2D"
    ):
        push_error("Generated resource property lost its concrete type hint")
        exported.free()
        quit(1)
        return
    var mode_info: Dictionary = property_by_name.movement_mode
    if (
        int(mode_info.get("hint", -1)) != PROPERTY_HINT_ENUM
        or str(mode_info.get("hint_string", "")) != "IDLE:0,WALK:4,RUN:8"
    ):
        push_error("Generated enum property lost its member metadata")
        exported.free()
        quit(1)
        return
    if not property_by_name.has("internal_counter"):
        push_error("Generated script-only accessor property is missing")
        exported.free()
        quit(1)
        return
    var internal_info: Dictionary = property_by_name.internal_counter
    var internal_usage := int(internal_info.get("usage", 0))
    if (
        (internal_usage & PROPERTY_USAGE_SCRIPT_VARIABLE) == 0
        or (internal_usage & PROPERTY_USAGE_STORAGE) != 0
        or (internal_usage & PROPERTY_USAGE_EDITOR) != 0
    ):
        push_error("Script-only accessor property has invalid usage flags")
        exported.free()
        quit(1)
        return
    exported.set("greeting", "Native property")
    exported.set("movement_speed", 24.5)
    exported.set("movement_mode", 8)
    exported.set("internal_counter", 9)
    var update_callable := Callable(exported, &"update_score")
    var nested_records := [{"tint": Color(0.2, 0.3, 0.4, 1.0)}]
    var dynamic_results := [
        ["method call", exported.call("dynamic_call", exported, 27), 27],
        ["property read after call", exported.call("dynamic_read", exported), 27],
        ["property write result", exported.call("dynamic_write", exported, 31), null],
        ["property read after write", exported.call("dynamic_read", exported), 31],
        ["compound property result", exported.call("dynamic_increment", exported, 5), null],
        ["property read after compound write", exported.call("dynamic_read", exported), 36],
        ["plain property write result", exported.call("dynamic_plain_write", exported, 12), null],
        ["plain property read after write", exported.call("dynamic_plain_read", exported), 12],
        ["builtin method call", exported.call("dynamic_size", "GDPP"), 4],
        ["key read", exported.call("dynamic_key_read", {"answer": 42}, "answer"), 42],
        [
            "key write",
            exported.call("dynamic_key_write", {"answer": 42}, "answer", 43),
            43,
        ],
        [
            "compound key write",
            exported.call("dynamic_key_increment", {"score": 30}, "score", 5),
            35,
        ],
        [
            "nested Node2D position component write",
            exported.call("dynamic_component_write", exported),
            Vector2(0.0, -3.0),
        ],
        [
            "Variant-held Color component write",
            exported.call("dynamic_variant_component_write"),
            Color(0.1, 0.2, 0.3, 0.25),
        ],
        [
            "script member Variant component write",
            exported.call("dynamic_member_component_write"),
            Color(0.4, 0.5, 0.6, 0.75),
        ],
        [
            "Transform3D/Vector3 deep component write",
            exported.call("dynamic_deep_component_write"),
            Vector3(4.0, 0.0, 0.0),
        ],
        [
            "Array/Dictionary/Color nested component write",
            exported.call("dynamic_record_component_write", nested_records),
            Color(0.2, 0.3, 0.4, 0.5),
        ],
        ["array iteration", exported.call("sum_dynamic", [1, 2, 3, 4]), 8],
        ["string iteration", exported.call("join_dynamic", "GDPP"), "GDPP"],
        ["typed Callable call", exported.call("invoke_callable", update_callable, 40), 40],
        [
            "dynamic Callable call",
            exported.call("invoke_dynamic_callable", update_callable, 41),
            41,
        ],
    ]
    for check: Array in dynamic_results:
        if check[1] != check[2]:
            push_error(
                "Generated dynamic %s failed: expected %s, got %s"
                % [check[0], check[2], check[1]]
            )
            exported.free()
            quit(1)
            return
    if (
        exported.get("greeting") != "Native property"
        or exported.get("movement_speed") != 24.5
        or exported.get("movement_mode") != 8
        or exported.get("internal_counter") != 9
        or exported.call("normalize_mode", 4) != 4
        or exported.call("classify_mode", 8) != "run"
        or exported.call("classify_mode", 4) != "slow"
        or exported.call("classify_mode", 99) != "unknown"
        or exported.call("classify_dynamic", "ready") != "ok"
        or exported.call("classify_dynamic", 42) != "other"
        or exported.call("is_node", exported) != true
        or exported.call("is_node", "not a node") != false
        or exported.call("is_string", "ready") != true
        or exported.call("is_string", exported) != false
        or exported.call("update_score", -5) != 0
        or exported.call("update_score", 35) != 35
    ):
        push_error("Generated export property accessors did not preserve values")
        exported.free()
        quit(1)
        return
    exported.free()
    print("GDPP_NATIVE_PROJECT_OK")
    quit(0)
