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


func _coroutine_signal_count(instance: Object) -> int:
    var count := 0
    for signal_info: Dictionary in instance.get_signal_list():
        if str(signal_info.get("name", "")).begins_with("__gdpp_coroutine_completed_"):
            count += 1
    return count


func _run() -> void:
    var player_class := _native_class_for("player.gd")
    var hello_class := _native_class_for("hello.gd")
    var inheritance_class := _native_class_for("inheritance_child.gd")
    var iteration_class := _native_class_for("iteration_case.gd")
    var cross_a_class := _native_class_for("cross_ref_a.gd")
    var cross_b_class := _native_class_for("cross_ref_b.gd")
    var optimization_class := _native_class_for("optimization_case.gd")
    var required_init_class := _native_class_for("required_initializer.gd")
    var rpc_class := _native_class_for("rpc_case.gd")
    var long_await_class := _native_class_for("long_await_chain.gd")
    var await_expression_class := _native_class_for("await_expression_case.gd")
    var typed_container_class := _native_class_for("typed_container_case.gd")
    var integer_semantics_class := _native_class_for("integer_semantics_case.gd")
    if (
        player_class.is_empty()
        or hello_class.is_empty()
        or inheritance_class.is_empty()
        or iteration_class.is_empty()
        or cross_a_class.is_empty()
        or cross_b_class.is_empty()
        or optimization_class.is_empty()
        or required_init_class.is_empty()
        or rpc_class.is_empty()
        or long_await_class.is_empty()
        or await_expression_class.is_empty()
        or typed_container_class.is_empty()
        or integer_semantics_class.is_empty()
    ):
        push_error("Generated native class manifest is incomplete")
        quit(1)
        return

    var native_iteration: Object = ClassDB.instantiate(iteration_class)
    var script_iteration: Object = load("res://iteration_case.gd").new()
    if native_iteration == null or script_iteration == null:
        push_error("Static iteration differential fixtures are unavailable")
        quit(1)
        return
    var native_iteration_trace: Array = native_iteration.call("run")
    var script_iteration_trace: Array = script_iteration.call("run")
    var expected_iteration_trace: Array = [
        1, 2, 3, 4, 5, 6, "A", "🙂", "B", "left", "right", "tail",
        0.0, 1.0, 2.0, 3.0,
        1.5, 2.5, 3.5,
        2, 3, 4,
        5.0, 3.0, 1.0,
        7, 4, 1,
        8.0, 9.0,
        &"alpha", &"beta",
        &"custom_0", &"custom_1", &"custom_2",
        0, null, true,
    ]
    if (
        native_iteration_trace != script_iteration_trace
        or native_iteration_trace != expected_iteration_trace
    ):
        push_error(
            "Native iteration differs from GDScript: native=%s script=%s"
            % [native_iteration_trace, script_iteration_trace]
        )
        quit(1)
        return
    native_iteration = null
    script_iteration = null
    var native_typed_container: Object = ClassDB.instantiate(typed_container_class)
    var script_typed_container: Object = load("res://typed_container_case.gd").new()
    if native_typed_container == null or script_typed_container == null:
        push_error("Typed-container differential fixtures are unavailable")
        quit(1)
        return
    var native_typed_report: Dictionary = native_typed_container.call("run")
    var script_typed_report: Dictionary = script_typed_container.call("run")
    if native_typed_report != script_typed_report:
        push_error(
            "Typed-container runtime differs from GDScript: native=%s script=%s"
            % [native_typed_report, script_typed_report]
        )
        native_typed_container = null
        script_typed_container = null
        quit(1)
        return
    native_typed_container = null
    script_typed_container = null
    var native_integers: Object = ClassDB.instantiate(integer_semantics_class)
    var script_integers: Object = load("res://integer_semantics_case.gd").new()
    if native_integers == null or script_integers == null:
        push_error("Integer-semantics differential fixtures are unavailable")
        quit(1)
        return
    var native_integer_report: Dictionary = native_integers.call("run")
    var expected_integer_report: Dictionary = {
        "add": -9223372036854775808,
        "subtract": 9223372036854775807,
        "multiply": -2,
        "negate": -9223372036854775808,
        "divide": -9223372036854775808,
        "modulo": 0,
        "shift_left_sign": -9223372036854775808,
        "shift_left_64": 1,
        "shift_left_negative_count": -9223372036854775808,
        "shift_right_negative": -4611686018427387904,
        "shift_right_64": 9223372036854775807,
        "bit_not": -1,
        "bit_and": 21930,
        "bit_or": 21930,
        "bit_xor": 21930,
        "compound": -9223372036854775808,
        "indexed": [-9223372036854775808],
        "dynamic_add": -9223372036854775808,
        "dynamic_subtract": 9223372036854775807,
        "dynamic_multiply": -2,
        "dynamic_shift_left_64": 1,
        "dynamic_shift_left_negative": -9223372036854775808,
    }
    if native_integer_report != expected_integer_report:
        push_error("Native integer semantics differ from the fixed contract: %s" % native_integer_report)
        quit(1)
        return
    var expected_safe_integer_report := expected_integer_report.duplicate()
    expected_safe_integer_report.erase("divide")
    expected_safe_integer_report.erase("modulo")
    var native_safe_integer_report: Dictionary = native_integers.call("run", false)
    var script_safe_integer_report: Dictionary = script_integers.call("run", false)
    if (
        native_safe_integer_report != script_safe_integer_report
        or native_safe_integer_report != expected_safe_integer_report
    ):
        push_error(
            "Native integer semantics differ from the cross-version GDScript oracle: native=%s script=%s"
            % [native_safe_integer_report, script_safe_integer_report]
        )
        quit(1)
        return
    var godot_version: Dictionary = Engine.get_version_info()
    # Godot 4.4.1 x86_64 traps inside its GDScript VM for INT64_MIN / -1 and modulo. The AOT
    # implementation is still required to execute and validate those edges on every target.
    if int(godot_version.get("minor", 0)) >= 5:
        var script_integer_report: Dictionary = script_integers.call("run")
        if script_integer_report != expected_integer_report:
            push_error(
                "GDScript integer semantics differ from the fixed contract: %s"
                % script_integer_report
            )
            quit(1)
            return
    native_integers = null
    script_integers = null
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

    var native_optimization: Object = ClassDB.instantiate(optimization_class)
    var script_optimization: Object = load("res://optimization_case.gd").new()
    if native_optimization == null or script_optimization == null:
        push_error("Control-flow optimization differential fixtures are unavailable")
        quit(1)
        return
    var native_trace: Array = native_optimization.call("run")
    var script_trace: Array = script_optimization.call("run")
    if native_trace != script_trace or native_trace != ["live"]:
        push_error(
            "Optimized native control flow differs from GDScript: native=%s script=%s"
            % [native_trace, script_trace]
        )
        quit(1)
        return

    var rpc_instance: Node = ClassDB.instantiate(rpc_class) as Node
    if rpc_instance == null:
        push_error("Generated native RPC class is unavailable")
        quit(1)
        return
    root.add_child(rpc_instance)
    var rpc_config: Dictionary
    if rpc_instance.has_method(&"get_node_rpc_config"):
        rpc_config = rpc_instance.call(&"get_node_rpc_config")
    else:
        rpc_config = rpc_instance.call(&"get_rpc_config")
    var defaults: Dictionary = rpc_config.get(&"default_rpc", {})
    var configured: Dictionary = rpc_config.get(&"configured_rpc", {})
    if (
        defaults.get("rpc_mode", -1) != MultiplayerAPI.RPC_MODE_AUTHORITY
        or defaults.get("transfer_mode", -1) != MultiplayerPeer.TRANSFER_MODE_UNRELIABLE
        or defaults.get("call_local", true) != false
        or defaults.get("channel", -1) != 0
        or configured.get("rpc_mode", -1) != MultiplayerAPI.RPC_MODE_ANY_PEER
        or configured.get("transfer_mode", -1) != MultiplayerPeer.TRANSFER_MODE_RELIABLE
        or configured.get("call_local", false) != true
        or configured.get("channel", -1) != 3
    ):
        push_error("Generated native RPC configuration differs from GDScript semantics: %s" % rpc_config)
        rpc_instance.queue_free()
        quit(1)
        return
    var rpc_error: Error = rpc_instance.rpc(&"configured_rpc", 73)
    if rpc_error != OK or rpc_instance.get("received_value") != 73:
        push_error(
            "Generated call-local RPC endpoint did not execute: error=%s value=%s"
            % [rpc_error, rpc_instance.get("received_value")]
        )
        rpc_instance.queue_free()
        quit(1)
        return
    rpc_instance.queue_free()

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

    var await_expression: Object = ClassDB.instantiate(await_expression_class)
    if await_expression == null:
        push_error("Generated await-expression class is unavailable")
        quit(1)
        return
    await_expression.call("run")
    if await_expression.get("trace") != [10]:
        push_error("Await expression evaluated its left operand in the wrong order")
        quit(1)
        return
    await_expression.emit_signal("selected", 5)
    if await_expression.get("trace") != [10, 15, 20]:
        push_error("Await expression did not restore its first result")
        quit(1)
        return
    await_expression.emit_signal("selected", 7)
    if (
        await_expression.get("trace") != [10, 15, 20, 30]
        or await_expression.get("values") != [20, 7, 30, 15]
    ):
        push_error("Await expression lost argument ordering or captured locals")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if await_expression.get("trace") != [10, 15, 20, 30, 40]:
        push_error("Await expression did not resume an if condition")
        quit(1)
        return

    await_expression.call("run_control_flow")
    if await_expression.get("control_trace") != [1]:
        push_error("Await short-circuit evaluated an operand out of order")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if await_expression.get("control_trace") != [1, 10, 0, -20, 2, 20]:
        push_error("Await short-circuit evaluated a skipped signal operand")
        quit(1)
        return
    await_expression.emit_signal("selected", false)
    if await_expression.get("control_trace") != [1, 10, 0, -20, 2, 20, 30, 30]:
        push_error("Await conditional evaluated the unselected true branch")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    await_expression.emit_signal("selected", 40)
    if await_expression.get("control_trace") != [1, 10, 0, -20, 2, 20, 30, 30, 40]:
        push_error("Await conditional lost its selected true-branch result")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if await_expression.get("control_trace").back() != 101:
        push_error("Await while condition did not enter its first iteration")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if (
        await_expression.get("control_trace").back() != 102
        or await_expression.get("control_result") != [true, false, true, 30, 40, 2]
    ):
        push_error(
            "Await loop break/continue or condition reevaluation is invalid: trace=%s result=%s"
            % [
                await_expression.get("control_trace"),
                await_expression.get("control_result"),
            ]
        )
        quit(1)
        return

    await_expression.call("run_parameter_loop", 0)
    await_expression.emit_signal("selected", true)
    if await_expression.get("parameter_result") != -1:
        push_error("Await loop completed before its parameter reached the recovery edge")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if await_expression.get("parameter_result") != 2:
        push_error("Await loop lost a function parameter across suspension")
        quit(1)
        return

    await_expression.set("awaited_property", 0)
    await_expression.emit_signal("selected", true)
    if await_expression.get("awaited_property") != 0:
        push_error("Await property setter completed before its recovery edge")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if await_expression.get("awaited_property") != 2:
        push_error("Await property setter lost its entry parameter across suspension")
        quit(1)
        return


    await_expression.call("run_match", 2)
    if await_expression.get("match_trace") != [102, 20]:
        push_error("Await match guard ran before binding or evaluated its selector repeatedly")
        quit(1)
        return
    await_expression.emit_signal("selected", false)
    if (
        await_expression.get("match_trace") != [102, 20, 20, 200, 999]
        or await_expression.get("match_result") != 20
    ):
        push_error("A false awaited match guard did not continue at the next branch")
        quit(1)
        return

    await_expression.call("run_match", 3)
    await_expression.emit_signal("selected", true)
    if (
        await_expression.get("match_trace") != [103, 30, 3]
        or await_expression.get("match_result") != -1
    ):
        push_error("Await match body ran out of order or skipped its suspension")
        quit(1)
        return
    await_expression.emit_signal("selected", true)
    if (
        await_expression.get("match_trace") != [103, 30, 3, 999]
        or await_expression.get("match_result") != 3
    ):
        push_error("Await match body did not resume into the outer continuation")
        quit(1)
        return
    if (
        await_expression.call("immediate_match", 1) != 10
        or await_expression.call("immediate_match", 2) != 20
    ):
        push_error("A non-suspending awaited match guard changed return semantics")
        quit(1)
        return

    await_expression.call("run_assert_success")
    await_expression.emit_signal("selected", true)
    if await_expression.get("assert_trace") != [1]:
        push_error("A successful awaited assert evaluated its lazy message or lost continuation")
        quit(1)
        return
    await_expression.emit_signal("selected", "unused message")
    if await_expression.get("assert_trace") != [1]:
        push_error("A successful awaited assert left a message signal callback connected")
        quit(1)
        return

    await_expression.call("run_assert_failure")
    await_expression.emit_signal("selected", false)
    if await_expression.get("assert_trace") != []:
        push_error("A failed awaited assert entered its business continuation before the message")
        quit(1)
        return
    await_expression.emit_signal("selected", "expected failure")
    if await_expression.get("assert_trace") != []:
        push_error("A failed awaited assert continued after reporting its lazy message")
        quit(1)
        return
    await_expression.emit_signal("selected", "unused message")
    if await_expression.get("assert_trace") != []:
        push_error("A failed awaited assert left a signal callback connected")
        quit(1)
        return

    await_expression.call("reset_coroutine_values")
    var first_pending: Variant = await_expression.call("consume_produced", 10)
    var second_pending: Variant = await_expression.call("consume_produced", 20)
    if (
        typeof(first_pending) != TYPE_SIGNAL
        or typeof(second_pending) != TYPE_SIGNAL
        or first_pending == second_pending
        or _coroutine_signal_count(await_expression) != 4
    ):
        push_error("Concurrent native coroutines did not expose isolated completion signals")
        quit(1)
        return
    await_expression.emit_signal("selected", 0)
    if (
        await_expression.get("coroutine_values") != [11, 21]
        or _coroutine_signal_count(await_expression) != 0
    ):
        push_error("Concurrent native coroutine results collided or leaked user signals")
        quit(1)
        return

    var void_pending: Variant = await_expression.call("consume_void")
    if typeof(void_pending) != TYPE_SIGNAL or _coroutine_signal_count(await_expression) != 2:
        push_error("A void native coroutine did not expose its pending completion")
        quit(1)
        return
    await_expression.emit_signal("selected", 0)
    if (
        await_expression.get("coroutine_values") != [11, 21, null]
        or _coroutine_signal_count(await_expression) != 0
    ):
        push_error("Awaiting a void native coroutine did not resume with null")
        quit(1)
        return

    var immediate_result: Variant = await_expression.call("consume_maybe_immediate", true)
    if (
        immediate_result != null
        or await_expression.get("coroutine_values") != [11, 21, null, 11]
        or _coroutine_signal_count(await_expression) != 0
    ):
        push_error("A synchronously completed native coroutine did not return directly")
        quit(1)
        return

    await_expression.call_deferred("emit_signal", &"selected", 0)
    var gdscript_awaited_value: Variant = await await_expression.call(
        "produce_after_signal", 30
    )
    if gdscript_awaited_value != 31:
        push_error(
            "GDScript could not await a value returned by a native coroutine: value=%s"
            % gdscript_awaited_value
        )
        quit(1)
        return
    await process_frame
    if _coroutine_signal_count(await_expression) != 0:
        push_error("A GDScript-awaited native coroutine leaked its completion signal")
        quit(1)
        return

    await_expression.call_deferred("emit_signal", &"selected", 0)
    var gdscript_awaited_void: Variant = await await_expression.call(
        "produce_void_after_signal"
    )
    if gdscript_awaited_void != null:
        push_error("GDScript could not await a void native coroutine")
        quit(1)
        return
    await process_frame
    if _coroutine_signal_count(await_expression) != 0:
        push_error("A void native coroutine leaked its completion signal")
        quit(1)
        return

    var gdscript_immediate: Variant = await await_expression.call(
        "produce_maybe_immediate", true
    )
    if gdscript_immediate != 11 or _coroutine_signal_count(await_expression) != 0:
        push_error("GDScript await changed an immediate native coroutine result")
        quit(1)
        return
    await_expression = null

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
    inherited.call_deferred("emit_signal", &"override_resumed")
    var overridden_result: Variant = await inherited.call("await_overridden_answer")
    if overridden_result != 42:
        push_error("An ABI-changing coroutine override did not resume its local caller")
        inherited.free()
        quit(1)
        return
    inherited.call_deferred("emit_signal", &"override_resumed")
    var base_dispatched_result: Variant = await inherited.call(
        "await_overridden_through_base", inherited
    )
    if base_dispatched_result != 42:
        push_error("A base-typed call did not dynamically dispatch to its coroutine override")
        inherited.free()
        quit(1)
        return
    await process_frame
    if _coroutine_signal_count(inherited) != 0:
        push_error("Coroutine override completion signals were not reclaimed")
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
    var cross_a_peers: Array = cross_a.call("typed_peers", [cross_b])
    var cross_b_peers: Dictionary = cross_b.call("typed_peers", {"primary": cross_a})
    if (
        not cross_a_peers.is_typed()
        or cross_a_peers.get_typed_class_name() != cross_b_class
        or cross_a_peers != [cross_b]
        or not cross_b_peers.is_typed_key()
        or not cross_b_peers.is_typed_value()
        or cross_b_peers.get_typed_key_builtin() != TYPE_STRING
        or cross_b_peers.get_typed_value_class_name() != cross_a_class
        or cross_b_peers.get("primary") != cross_a
    ):
        push_error("Generated cross-script typed containers lost native class identity")
        cross_a.free()
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
        "greeting",
        "movement_speed",
        "accent",
        "icon",
        "movement_mode",
        "accessor_score",
        "typed_integers",
        "typed_weights",
        "typed_nodes",
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
    var integers_info: Dictionary = property_by_name.typed_integers
    var weights_info: Dictionary = property_by_name.typed_weights
    var nodes_info: Dictionary = property_by_name.typed_nodes
    if (
        int(integers_info.get("hint", -1)) != PROPERTY_HINT_ARRAY_TYPE
        or str(integers_info.get("hint_string", "")) != "int"
        or int(weights_info.get("hint", -1)) != PROPERTY_HINT_DICTIONARY_TYPE
        or str(weights_info.get("hint_string", "")) != "String;int"
        or int(nodes_info.get("hint", -1)) != PROPERTY_HINT_ARRAY_TYPE
        or str(nodes_info.get("hint_string", "")) != "Node"
    ):
        push_error("Generated typed-container properties lost their ClassDB metadata")
        exported.free()
        quit(1)
        return
    if exported.call("validate_typed_containers") != true:
        push_error("Generated typed containers lost runtime type metadata")
        exported.free()
        quit(1)
        return
    var roundtrip_info: Dictionary = {}
    for method: Dictionary in exported.get_method_list():
        if str(method.get("name", "")) == "typed_container_roundtrip":
            roundtrip_info = method
            break
    var roundtrip_arguments: Array = roundtrip_info.get("args", [])
    var roundtrip_return: Dictionary = roundtrip_info.get("return", {})
    if (
        roundtrip_arguments.size() != 1
        or int(roundtrip_arguments[0].get("hint", -1)) != PROPERTY_HINT_ARRAY_TYPE
        or str(roundtrip_arguments[0].get("hint_string", "")) != "int"
        or int(roundtrip_return.get("hint", -1)) != PROPERTY_HINT_DICTIONARY_TYPE
        or str(roundtrip_return.get("hint_string", "")) != "String;int"
    ):
        push_error("Generated typed-container method lost its ClassDB ABI metadata")
        exported.free()
        quit(1)
        return
    var typed_signal_info: Dictionary = {}
    for signal_info: Dictionary in exported.get_signal_list():
        if str(signal_info.get("name", "")) == "typed_containers_changed":
            typed_signal_info = signal_info
            break
    var typed_signal_arguments: Array = typed_signal_info.get("args", [])
    if (
        typed_signal_arguments.size() != 2
        or int(typed_signal_arguments[0].get("hint", -1)) != PROPERTY_HINT_ARRAY_TYPE
        or str(typed_signal_arguments[0].get("hint_string", "")) != "int"
        or int(typed_signal_arguments[1].get("hint", -1))
        != PROPERTY_HINT_DICTIONARY_TYPE
        or str(typed_signal_arguments[1].get("hint_string", "")) != "String;int"
    ):
        push_error("Generated typed-container signal lost its ClassDB ABI metadata")
        exported.free()
        quit(1)
        return
    var typed_result: Dictionary = exported.call("typed_container_roundtrip", [1, 2, 3])
    if (
        typed_result.get("size", -1) != 3
        or not typed_result.is_typed_key()
        or not typed_result.is_typed_value()
        or typed_result.get_typed_key_builtin() != TYPE_STRING
        or typed_result.get_typed_value_builtin() != TYPE_INT
    ):
        push_error("Typed container method ABI did not preserve its return contract")
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
        or exported.call("validate_latest_literals") != true
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
