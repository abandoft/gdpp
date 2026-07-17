#include "support/test.hpp"

#include "gdpp/compiler.hpp"

#include <algorithm>
#include <string>

TEST_CASE("compiler generates bindable GDExtension C++") {
    const std::string source = "extends Node\n"
                               "class_name Counter\n"
                               "signal changed(value: int)\n"
                               "var value: int = 0\n"
                               "func increment(amount: int) -> int:\n"
                               "    value += amount\n"
                               "    emit_signal(\"changed\", value)\n"
                               "    return value\n";
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("counter.gd", source);

    REQUIRE(result.success);
    REQUIRE_EQ(result.unit.script_class_name, std::string{"Counter"});
    REQUIRE_EQ(result.unit.class_name, std::string{"GDPPNative_Counter"});
    REQUIRE(result.unit.header.find("GDCLASS(GDPPNative_Counter, godot::Node)") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("ClassDB::bind_method") != std::string::npos);
    REQUIRE(result.unit.source.find("value = (value + amount)") != std::string::npos);
    REQUIRE(result.unit.source.find("ADD_SIGNAL") != std::string::npos);
}

TEST_CASE("compiler generates debug-only typed assert control flow") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("res://assertions.gd",
                                         "extends Node\n"
                                         "func validate(value: int) -> int:\n"
                                         "    assert(value > 0, \"positive value required\")\n"
                                         "    return value\n"
                                         "func validate_void(value: bool) -> void:\n"
                                         "    assert(value)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("#ifdef DEBUG_ENABLED") != std::string::npos);
    REQUIRE(result.unit.source.find("ERR_FAIL_COND_V_EDMSG") != std::string::npos);
    REQUIRE(result.unit.source.find("ERR_FAIL_COND_EDMSG") != std::string::npos);
    REQUIRE(result.unit.source.find("Assertion failed at res://assertions.gd:3") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("positive value required") != std::string::npos);
}

TEST_CASE("compiler emits nil fallthrough only for dynamic functions that need it") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("dynamic_returns.gd", "func side_effect(value):\n"
                                                               "    print(value)\n"
                                                               "func identity(value):\n"
                                                               "    return value\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("GDPPNative_DynamicReturns::side_effect") != std::string::npos);
    REQUIRE(result.unit.source.find("print(_gdpp_utility_argument_") != std::string::npos);
    REQUIRE(result.unit.source.find("    return {};\n}") != std::string::npos);
    REQUIRE(result.unit.source.find("return value;\n    return {};") == std::string::npos);
}

TEST_CASE("compiler rejects invalid assert forms and types") {
    const gdpp::Compiler compiler;
    const auto truthy_condition =
        compiler.compile("condition.gd", "func test() -> void:\n    assert(1)\n");
    const auto invalid_message =
        compiler.compile("message.gd", "func test() -> void:\n    assert(true, 42)\n");
    const auto expression =
        compiler.compile("expression.gd", "func test() -> void:\n    var result := assert(true)\n");

    REQUIRE(truthy_condition.success);
    REQUIRE(!invalid_message.success);
    REQUIRE(!expression.success);
    REQUIRE(truthy_condition.unit.source.find(
                "static_cast<bool>(godot::Variant(static_cast<int64_t>(1)))") != std::string::npos);
}

TEST_CASE("compiler applies warning directives and structured await to property setters") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("accessor_await.gd", "extends Node\n"
                                              "var score: int = 0:\n"
                                              "    set(value):\n"
                                              "        score = value\n"
                                              "        await get_tree().process_frame\n"
                                              "        score = value\n"
                                              "func ignored_unreachable() -> void:\n"
                                              "    return\n"
                                              "    @warning_ignore(\"unreachable_code\")\n"
                                              "    print(\"intentionally disabled\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("await_signal") != std::string::npos);
}

TEST_CASE("compiler converts packed arrays to compatible typed Array parameters") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("packed_array_argument.gd",
                                         "func consume(values: Array[String]) -> void:\n"
                                         "    pass\n"
                                         "func forward(values: PackedStringArray) -> void:\n"
                                         "    consume(values)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Array(") != std::string::npos);
}

TEST_CASE("compiler generates serializable Godot export properties and inspector hints") {
    const std::string source =
        "extends Node\n"
        "class_name ExportedSettings\n"
        "@export var title: String = \"Player\"\n"
        "@export_range(-10.0, 100.0, 0.5, \"or_greater\") var speed: float = 4.0\n"
        "@export_enum(\"Idle\", \"Run:4\") var state: int = 0\n"
        "@export_flags(\"Fire\", \"Ice\") var abilities: int = 0\n"
        "@export_file(\"*.json\") var config_path: String = \"\"\n"
        "@export_multiline var biography: String = \"\"\n"
        "@export_color_no_alpha var tint: Color = Color.html(\"ff8800\")\n"
        "@export_node_path(\"Node2D\") var target: NodePath\n"
        "@export var icon: Texture2D\n"
        "@export_category(\"Advanced\")\n"
        "@export var retries: int = 3\n"
        "@export var inferred_count = 5\n"
        "@export var inferred_tags = [\"commercial\"]\n";
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("exported_settings.gd", source);

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("_gdpp_get_title()") != std::string::npos);
    REQUIRE(result.unit.source.find("ADD_PROPERTY") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_HINT_RANGE") != std::string::npos);
    REQUIRE(result.unit.source.find("-10.0,100.0,0.5,or_greater") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_HINT_ENUM") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_HINT_FLAGS") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_HINT_NODE_PATH_VALID_TYPES") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_HINT_RESOURCE_TYPE") != std::string::npos);
    REQUIRE(result.unit.header.find("godot::Ref<godot::Texture2D> icon{}") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_USAGE_CATEGORY") != std::string::npos);
    REQUIRE(result.unit.header.find("godot::Variant inferred_count{}") != std::string::npos);
    REQUIRE(result.unit.source.find("inferred_count = static_cast<int64_t>(5)") !=
            std::string::npos);
    REQUIRE(
        result.unit.source.find("godot::PropertyInfo(godot::Variant::INT, \"inferred_count\"") !=
        std::string::npos);
    REQUIRE(
        result.unit.source.find("godot::PropertyInfo(godot::Variant::ARRAY, \"inferred_tags\"") !=
        std::string::npos);
}

TEST_CASE("compiler preserves annotation-implied exports and Godot object conversions") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "godot_conversions.gd",
        "extends Node2D\n"
        "@export_color_no_alpha var untyped_color\n"
        "var packed_color: Color = Color(0, 0)\n"
        "var texture: Texture2D\n"
        "func submit(canvas: RID) -> void:\n"
        "    RenderingServer.canvas_item_add_texture_rect_region(canvas, Rect2(), texture, "
        "Rect2())\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("godot::Variant untyped_color{}") != std::string::npos);
    REQUIRE(
        result.unit.source.find("godot::PropertyInfo(godot::Variant::COLOR, \"untyped_color\"") !=
        std::string::npos);
    REQUIRE(result.unit.source.find("godot::Color::hex") != std::string::npos);
    REQUIRE(result.unit.source.find("->get_rid()") != std::string::npos);
}

TEST_CASE("compiler generates registered internal classes and native lambda Callables") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("modern.gd", "extends Node\n"
                                                      "class Payload:\n"
                                                      "    var value: int\n"
                                                      "    func _init(initial: int) -> void:\n"
                                                      "        value = initial\n"
                                                      "signal changed(value: int)\n"
                                                      "func attach() -> int:\n"
                                                      "    var captured := 2\n"
                                                      "    changed.connect(\n"
                                                      "        func(value: int) -> void:\n"
                                                      "            print(value + captured))\n"
                                                      "    var payload := Payload.new(3)\n"
                                                      "    return payload.value\n");

    REQUIRE(result.success);
    REQUIRE_EQ(result.unit.inner_class_names.size(), std::size_t{1});
    REQUIRE_EQ(result.unit.inner_class_names.front(), std::string{"GDPPNative_Modern__Payload"});
    REQUIRE(result.unit.header.find("GDCLASS(GDPPNative_Modern__Payload, godot::RefCounted)") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::make_local_callable(this, 1, 1") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("InternalClassResource<GDPPNative_Modern__Payload>") !=
            std::string::npos);
}

TEST_CASE("static lambdas support defaults without binding an instance owner") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("factory.gd", "static func make() -> Callable:\n"
                                                       "    return func(value: int = 1) -> int:\n"
                                                       "        return value + 1\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::make_local_callable(nullptr, 0, 1") !=
            std::string::npos);
    REQUIRE(result.unit.source.find(".size() > 0 ?") != std::string::npos);
}

TEST_CASE("compiler preserves instance defaults and explicit null through native bindings") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("defaults.gd", "extends Node\n"
                                        "var fallback = [1]\n"
                                        "func choose(pool = fallback, focus: Control = null):\n"
                                        "    return pool if focus == null else focus\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find(
                "godot::Variant _gdpp_argument_pool = gdpp::runtime::default_argument()") !=
            std::string::npos);
    REQUIRE(result.unit.header.find(
                "godot::Variant _gdpp_argument_focus = gdpp::runtime::default_argument()") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::is_default_argument(_gdpp_argument_pool) ? "
                                    "godot::Variant(fallback)") != std::string::npos);
    REQUIRE(result.unit.source.find("DEFVAL(gdpp::runtime::default_argument())") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("DEFVAL(godot::Variant())") == std::string::npos);
    REQUIRE(result.unit.source.find("? nullptr : godot::Object::cast_to<godot::Control>") !=
            std::string::npos);
}

TEST_CASE("compiler gives dynamic conditional branches an unambiguous native common type") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("conditional.gd", "func ratio(value, enabled: bool):\n"
                                           "    return value if enabled else 0.0\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("? godot::Variant(value) : godot::Variant(0.0)") !=
            std::string::npos);
}

TEST_CASE("compiler resolves forward constants before field initializers") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("forward_constant.gd", "var speed = MIN_SPEED\n"
                                                                "const MIN_SPEED = TAU * 0.25\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("static const double MIN_SPEED;") != std::string::npos);
    REQUIRE(result.unit.source.find("speed = MIN_SPEED;") != std::string::npos);
    REQUIRE(result.unit.source.find("speed = MIN_SPEED();") == std::string::npos);
}

TEST_CASE("compiler emits void returns inside structured await continuations") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "await_return.gd", "extends Node\n"
                           "signal resumed\n"
                           "var cancelled = false\n"
                           "func run():\n"
                           "    await resumed\n"
                           "    if cancelled:\n"
                           "        return\n"
                           "    var active = [1].filter(func(value): return value > 0)\n"
                           "    print(\"done\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("if (static_cast<bool>(godot::Variant(cancelled))) {\n"
                                    "            return;\n") != std::string::npos);
    REQUIRE(result.unit.source.find("mutable -> godot::Variant {\n"
                                    "    godot::Variant value =") != std::string::npos);
    REQUIRE(result.unit.source.find("return godot::Variant(static_cast<bool>(") !=
            std::string::npos);
}

TEST_CASE("compiler routes native script identity through the compatibility runtime") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("identity.gd", "extends Node\n"
                                                        "func identity():\n"
                                                        "    return get_script()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::script_identity(this)") != std::string::npos);
}

TEST_CASE("compiler selects the Godot Node template for implicit get_node calls") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("node_lookup.gd", "extends Node\n"
                                           "func find_child():\n"
                                           "    return get_node(\"Child\")\n"
                                           "func find_optional():\n"
                                           "    return get_node_or_null(\"Child\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("get_node<godot::Node>(") != std::string::npos);
    REQUIRE(result.unit.source.find("get_node_or_null(") != std::string::npos);
    REQUIRE(result.unit.source.find("get_node_or_null<") == std::string::npos);
}

TEST_CASE("compiler lowers inherited Godot signals on self and typed objects") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("button_signals.gd", "extends Button\n"
                                              "func wire(other: Button) -> void:\n"
                                              "    pressed.connect(on_pressed)\n"
                                              "    resized.connect(on_resized)\n"
                                              "    other.pressed.connect(on_pressed)\n"
                                              "func on_pressed() -> void:\n"
                                              "    pass\n"
                                              "func on_resized() -> void:\n"
                                              "    pass\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Signal(this, godot::StringName(\"pressed\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Signal(this, godot::StringName(\"resized\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Signal(other, godot::StringName(\"pressed\"))") !=
            std::string::npos);
}

TEST_CASE("compiler emits local signals through their owner without temporary Signal objects") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("signal_emit.gd", "extends Node\n"
                                                           "signal pulse(value: int)\n"
                                                           "func fire(value: int) -> void:\n"
                                                           "    pulse.emit(value + 1)\n"
                                                           "    self.pulse.emit(value + 2)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("static const godot::StringName _gdpp_signal_name_") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("this->emit_signal(_gdpp_signal_name_") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Signal(this, godot::StringName(\"pulse\")).emit") ==
            std::string::npos);
}

TEST_CASE("compiler rejects nested internal classes transactionally") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("nested.gd", "class Outer:\n"
                                                      "    class Inner:\n"
                                                      "        var value: int\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
    REQUIRE(std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                         [](const auto& item) { return item.code == "GDS4100"; }) !=
            result.diagnostics.end());
}

TEST_CASE("compiler rejects invalid and unsupported export annotations") {
    const gdpp::Compiler compiler;
    const auto wrong_type =
        compiler.compile("wrong.gd", "@export_range(0, 10) var label: String = \"bad\"\n");
    const auto dynamic = compiler.compile("dynamic.gd", "@export var value: Variant\n");
    const auto unsupported = compiler.compile("rpc.gd", "@rpc var value: int = 1\n");

    REQUIRE(!wrong_type.success);
    REQUIRE(!dynamic.success);
    REQUIRE(!unsupported.success);
    REQUIRE(wrong_type.unit.source.empty());
}

TEST_CASE("compiler initializes onready fields immediately before ready") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("hud.gd", "extends Node\n"
                                   "@export_group(\"Nodes\", \"node_\")\n"
                                   "@onready var node_label: Label = $Label as Label\n"
                                   "func _ready() -> void:\n"
                                   "    node_label.text = \"ready\"\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("godot::Label* node_label{}") != std::string::npos);
    const auto initialization = result.unit.source.find("node_label =");
    const auto user_body = result.unit.source.find("node_label->set_text");
    REQUIRE(initialization != std::string::npos);
    REQUIRE(user_body != std::string::npos);
    REQUIRE(initialization < user_body);
    REQUIRE(result.unit.source.find("ADD_GROUP(\"Nodes\", \"node_\")") != std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"_ready\"") == std::string::npos);
}

TEST_CASE("compiler preloads member resources before instances are constructed") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "preloaded_scene.gd", "extends Node\nvar scene = preload(\"res://effects/spark.tscn\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("static void _gdpp_preload_resources()") != std::string::npos);
    REQUIRE(result.unit.header.find("static godot::Variant& _gdpp_preloaded_scene()") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("scene = _gdpp_preloaded_scene();") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_preloaded_scene() = godot::Ref<godot::PackedScene>(") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::load_resource(") != std::string::npos);
    REQUIRE(result.unit.header.find("static void _gdpp_release_preloaded_resources()") !=
            std::string::npos);
    REQUIRE(
        result.unit.source.find("_gdpp_preloaded_scene() = "
                                "std::remove_reference_t<decltype(_gdpp_preloaded_scene())>{};") !=
        std::string::npos);
    REQUIRE(result.unit.source.find("if (!gdpp_editor_hint) _gdpp_preload_resources();") !=
            std::string::npos);
}

TEST_CASE("compiler releases script static storage before Godot servers stop") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("static_cache.gd", "extends Node\n"
                                                            "static var cached_values: Array = []\n"
                                                            "static var cached_node: Node\n");

    REQUIRE(result.success);
    const auto release = result.unit.source.find("::_gdpp_release_preloaded_resources() {");
    REQUIRE(release != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_static_cached_values_release();", release) !=
            std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_static_cached_node_release();", release) !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant GDPPNative_StaticCache::cached_values") ==
            std::string::npos);
    REQUIRE(result.unit.source.find("static std::atomic<godot::Array*> value{nullptr}") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static std::atomic<godot::Node**> value{nullptr}") !=
            std::string::npos);
}

TEST_CASE("compiler lazily initializes and explicitly releases resource constants") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "constant_scene.gd", "extends Node\nconst SCENE = preload(\"res://effects/spark.tscn\")\n"
                             "func scene_resource():\n    return SCENE\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("static const godot::Ref<godot::PackedScene>& SCENE()") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_constant_SCENE_storage()") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_constant_SCENE_ready()") != std::string::npos);
    const auto getter = result.unit.source.find("::SCENE()");
    REQUIRE(getter != std::string::npos);
    REQUIRE(result.unit.source.find("SCENE()", getter + std::string{"::SCENE()"}.size()) !=
            std::string::npos);
    const auto release = result.unit.source.find("::_gdpp_release_preloaded_resources() {");
    REQUIRE(release != std::string::npos);
    REQUIRE(result.unit.source.find(
                "_gdpp_constant_SCENE_storage() = "
                "std::remove_reference_t<decltype(_gdpp_constant_SCENE_storage())>{};",
                release) != std::string::npos);
}

TEST_CASE("compiler defers instance initialization while the editor exports scenes") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "service_state.gd",
        "extends Node\nvar service_value = Engine.get_singleton(\"CustomerService\").value\n");

    REQUIRE(result.success);
    const auto editor_hint =
        result.unit.source.find("const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();");
    const auto guard = result.unit.source.find("if (!gdpp_editor_hint) {");
    const auto initialization = result.unit.source.find("service_value =");
    const auto guard_end = result.unit.source.find("    }", initialization);
    REQUIRE(editor_hint != std::string::npos);
    REQUIRE(guard != std::string::npos);
    REQUIRE(initialization != std::string::npos);
    REQUIRE(guard_end != std::string::npos);
    REQUIRE(editor_hint < guard);
    REQUIRE(guard < initialization);
    REQUIRE(initialization < guard_end);
}

TEST_CASE("compiler exposes pure field defaults while keeping service initializers deferred") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("scene_defaults.gd",
                         "extends Node\n"
                         "@export var speed: int = 300\n"
                         "@export var offset: Vector2 = Vector2(2.0, 4.0)\n"
                         "var service_value = Engine.get_singleton(\"CustomerService\").value\n");

    REQUIRE(result.success);
    const auto editor_hint =
        result.unit.source.find("const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();");
    const auto speed = result.unit.source.find("speed = static_cast<int64_t>(300);");
    const auto offset = result.unit.source.find("offset =");
    const auto guard = result.unit.source.find("if (!gdpp_editor_hint) {");
    const auto service = result.unit.source.find("service_value =", guard);
    REQUIRE(editor_hint != std::string::npos);
    REQUIRE(speed != std::string::npos);
    REQUIRE(offset != std::string::npos);
    REQUIRE(guard != std::string::npos);
    REQUIRE(service != std::string::npos);
    REQUIRE(editor_hint < speed);
    REQUIRE(speed < offset);
    REQUIRE(offset < guard);
    REQUIRE(guard < service);
    REQUIRE(result.unit.source.find("speed = static_cast<int64_t>(300);", speed + 1) ==
            std::string::npos);
    const auto later_offset = result.unit.source.find("offset =", offset + 1);
    REQUIRE(later_offset == std::string::npos || service < later_offset);
}

TEST_CASE("compiler preserves UTF-8 and unique-node paths in generated Godot strings") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("unicode_nodes.gd", "extends Node\n"
                                             "func locate() -> void:\n"
                                             "    var volume = %\"全局音量条\"\n"
                                             "    volume.set(\"标题\", \"中文\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::NodePath(godot::String::utf8(\"%全局音量条\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::String::utf8(\"中文\")") != std::string::npos);
}

TEST_CASE("compiler lowers inherited methods as first-class callables") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("deferred_child.gd", "extends Node\n"
                                              "func defer_child(child: Node) -> void:\n"
                                              "    add_child.call_deferred(child)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Callable(this, godot::StringName(\"add_child\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find(".call_deferred(") != std::string::npos);
}

TEST_CASE("compiler emits explicit engine super method dispatch") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("super.gd", "extends Node\n"
                                                     "func release_later() -> void:\n"
                                                     "    super.queue_free()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Node::queue_free()") != std::string::npos);
}

TEST_CASE("compiler lowers top-level signal awaits to lifetime-aware continuations") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("await.gd", "extends Node\n"
                                                     "signal resumed(value: int)\n"
                                                     "func run(input: int) -> void:\n"
                                                     "    var captured := input + 1\n"
                                                     "    await resumed\n"
                                                     "    print(captured)\n"
                                                     "    await resumed\n"
                                                     "    print(captured + 1)\n");

    REQUIRE(result.success);
    const auto first_await = result.unit.source.find("gdpp::runtime::await_signal");
    REQUIRE(first_await != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::await_signal", first_await + 1) !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Signal(this, godot::StringName(\"resumed\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("[=](const godot::Array &) mutable") != std::string::npos);
    REQUIRE(result.unit.source.find("= captured;") != std::string::npos);
}

TEST_CASE("compiler flattens long await chains into bounded MIR dispatch") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("long_await_chain.gd", "extends Node\n"
                                                                "signal resumed\n"
                                                                "func run(enabled: bool) -> void:\n"
                                                                "    await resumed\n"
                                                                "    await resumed\n"
                                                                "    await resumed\n"
                                                                "    if enabled:\n"
                                                                "        await resumed\n"
                                                                "        await resumed\n"
                                                                "    else:\n"
                                                                "        await resumed\n"
                                                                "    await resumed\n"
                                                                "    await resumed\n"
                                                                "    await resumed\n"
                                                                "    await resumed\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("using _gdpp_async_step_type_") != std::string::npos);
    REQUIRE(result.unit.source.find("switch (_gdpp_async_pc_") != std::string::npos);
    REQUIRE(result.unit.source.find("std::weak_ptr<_gdpp_async_step_type_") != std::string::npos);
    REQUIRE(result.unit.source.find("[=](const godot::Array &) mutable") == std::string::npos);
    std::size_t await_count = 0;
    for (std::size_t offset = 0; (offset = result.unit.source.find("gdpp::runtime::await_signal",
                                                                   offset)) != std::string::npos;
         offset += 1) {
        ++await_count;
    }
    REQUIRE_EQ(await_count, std::size_t{10});
}

TEST_CASE("compiler restores signal arguments for await-initialized locals") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("await_value.gd", "extends Node\n"
                                                           "signal selected(value: String)\n"
                                                           "func choose():\n"
                                                           "    var side = await selected\n"
                                                           "    print(side)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::await_result(") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant side =") != std::string::npos);
    REQUIRE(result.unit.source.find("= side; godot::UtilityFunctions::print(") !=
            std::string::npos);
}

TEST_CASE("compiler applies truthiness to typed containers with short circuiting") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("container_truth.gd", "extends Node\n"
                                               "var items: Array[int] = []\n"
                                               "func has_items() -> bool:\n"
                                               "    return items && items.size()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("static_cast<bool>(godot::Variant(items)) &&") !=
            std::string::npos);
}

TEST_CASE("compiler infers exported PackedScene resources from preload paths") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "scene_export.gd", "extends Node\n"
                           "@export var projectile_scene = preload(\"res://projectile.tscn\")\n");

    REQUIRE(result.success);
    REQUIRE(
        result.unit.source.find("PROPERTY_HINT_RESOURCE_TYPE, godot::String(\"PackedScene\")") !=
        std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::load_resource(") != std::string::npos);
}

TEST_CASE("compiler permits fire-and-forget awaits in dynamic lambdas") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("lambda_await.gd", "extends Node\n"
                                                            "signal resumed\n"
                                                            "func callback() -> Callable:\n"
                                                            "    return func():\n"
                                                            "        await resumed\n"
                                                            "        print(1)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::await_signal") != std::string::npos);
}

TEST_CASE("compiler warns when dynamic coroutine values become fire-and-forget") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("dynamic_coroutine.gd", "extends Node\n"
                                                                 "signal resumed\n"
                                                                 "func spawn():\n"
                                                                 "    await resumed\n"
                                                                 "    return 42\n");

    REQUIRE(result.success);
    REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                        [](const gdpp::Diagnostic& diagnostic) {
                            return diagnostic.severity == gdpp::DiagnosticSeverity::warning &&
                                   diagnostic.code == "GDS4103";
                        }));
}

TEST_CASE("compiler supports structured awaits and rejects unsafe coroutine contracts") {
    const gdpp::Compiler compiler;
    const auto nested = compiler.compile("nested_await.gd", "signal resumed\n"
                                                            "func run():\n"
                                                            "    if true:\n"
                                                            "        await resumed\n"
                                                            "    for index in range(3):\n"
                                                            "        await resumed\n"
                                                            "        print(index)\n");
    const auto non_void = compiler.compile("value_await.gd", "signal resumed\n"
                                                             "func run() -> int:\n"
                                                             "    await resumed\n"
                                                             "    return 1\n");
    const auto static_await = compiler.compile("static_await.gd", "signal resumed\n"
                                                                  "static func run() -> void:\n"
                                                                  "    await resumed\n");
    const auto nonsignal = compiler.compile("bad_await.gd", "func run() -> void:\n"
                                                            "    await 42\n");
    const auto initializer = compiler.compile("init_await.gd", "signal resumed\n"
                                                               "func _init() -> void:\n"
                                                               "    await resumed\n");

    REQUIRE(nested.success);
    REQUIRE(nested.unit.source.find("std::make_shared<std::function<void()>>") !=
            std::string::npos);
    REQUIRE(nested.unit.source.find("gdpp::runtime::iter_next") != std::string::npos);
    REQUIRE(!non_void.success);
    REQUIRE(!static_await.success);
    REQUIRE(!nonsignal.success);
    REQUIRE(!initializer.success);
    bool found_initializer_await = false;
    for (const auto& diagnostic : initializer.diagnostics)
        found_initializer_await = found_initializer_await || diagnostic.code == "GDS4097";
    REQUIRE(found_initializer_await);
}

TEST_CASE("compiler applies GDScript truthiness to RefCounted objects") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("resource_truth.gd", "extends Node\n"
                                                              "var shape: BoxShape3D\n"
                                                              "func missing_shape() -> bool:\n"
                                                              "    return not shape\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("(!(shape).is_valid())") != std::string::npos);
}

TEST_CASE("compiler synthesizes ready for onready fields") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("camera.gd", "extends Node\n@onready var camera := $Camera\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("virtual void _ready() override;") != std::string::npos);
    REQUIRE(result.unit.source.find("::_ready()") != std::string::npos);
    REQUIRE(result.unit.source.find("camera = godot::Variant(get_node") != std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"_ready\"") == std::string::npos);
}

TEST_CASE("compiler preserves explicit typed iterator variables") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("typed_for.gd", "func sum(values: Array[int]) -> int:\n"
                                                         "    var total: int = 0\n"
                                                         "    for value: int in values:\n"
                                                         "        total += value\n"
                                                         "    return total\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_init") != std::string::npos);
    REQUIRE(result.unit.source.find("int64_t value = static_cast<int64_t>(godot::Variant(") !=
            std::string::npos);
}

TEST_CASE("compiler iterates packed arrays with their Godot element types") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "packed_for.gd", "func join_arguments() -> String:\n"
                         "    var result := \"\"\n"
                         "    var arguments: PackedStringArray = OS.get_cmdline_user_args()\n"
                         "    for argument: String in arguments:\n"
                         "        result += argument\n"
                         "    return result\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::PackedStringArray arguments") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::String argument =") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_packed_iterable_") != std::string::npos);
    REQUIRE(result.unit.source.find(".size();") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_init") == std::string::npos);
}

TEST_CASE("compiler preserves typed subscript and builtin component scalar semantics") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "typed_subscripts.gd",
        "func update(values: Array[int], packed: PackedInt64Array, vector: Vector2) -> float:\n"
        "    values[0] += 2\n"
        "    packed[0] += 3\n"
        "    return vector.x * vector.x + float(values[0] + packed[0])\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("_gdpp_subscript_container_") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<int64_t>(godot::Variant(") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<int64_t>(_gdpp_subscript_container_") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<double>(vector.x)") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::binary") == std::string::npos);
}

TEST_CASE("compiler preallocates Array literals and evaluates elements in source order") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("array_literal.gd",
                                         "func build(value: int) -> Array[int]:\n"
                                         "    return [value, value + 1, value + 2, value + 3]\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find(".resize(4)") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_array_value_") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Array::make") == std::string::npos);
}

TEST_CASE("compiler lowers direct range loops without allocating a temporary Array") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("range_for.gd", "func sum() -> int:\n"
                                                         "    var total := 0\n"
                                                         "    for value in range(1, 10, 2):\n"
                                                         "        total += value\n"
                                                         "    return total\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("_gdpp_range_start_") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_range_stop_") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_range_step_") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::make_range") == std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_init") == std::string::npos);
}

TEST_CASE("compiler defines static script fields outside the generated header") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("cache.gd", "class_name Cache\n"
                                                     "static var count: int = 1\n"
                                                     "static func increment() -> int:\n"
                                                     "    count += 1\n"
                                                     "    return count\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("static int64_t& _gdpp_static_count_storage()") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("value = new int64_t(static_cast<int64_t>(1))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_static_count_storage() =") != std::string::npos);
    REQUIRE(result.unit.source.find("int64_t GDPPNative_Cache::count = 1") == std::string::npos);
    REQUIRE(result.unit.header.find("static int64_t _gdpp_get_count()") != std::string::npos);
}

TEST_CASE("compiler resolves versioned builtin value constants") {
    const gdpp::Compiler compiler;
    gdpp::CompileOptions options;
    options.target_version = gdpp::GodotVersion::v4_7;
    const auto result = compiler.compile("constants.gd",
                                         "var color := Color.GRAY\n"
                                         "var direction: Vector3 = Vector3.UP\n",
                                         options);

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Color(0.74509805") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Vector3(0, 1, 0)") != std::string::npos);
}

TEST_CASE("compiler lowers negated membership through the Variant operator") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("membership.gd", "func missing(value: Variant, items: Array) -> bool:\n"
                                          "    return value not in items\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find(
                "!(static_cast<bool>(gdpp::runtime::binary(godot::Variant::OP_IN") !=
            std::string::npos);
}

TEST_CASE("compiler applies Godot-compatible numeric and builtin conversions") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("commercial_conversions.gd",
                                         "extends Sprite2D\n"
                                         "func configure(bounds: Vector4) -> int:\n"
                                         "    self_modulate = \"bcbcbc\"\n"
                                         "    return randi_range(bounds.x, bounds.y)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Color(godot::String(\"bcbcbc\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<int64_t>") != std::string::npos);
}

TEST_CASE("compiler infers native Godot virtual signatures and escapes C++ keywords") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("virtuals.gd", "extends Node\n"
                                                        "func _ready():\n"
                                                        "    pass\n"
                                                        "func _process(delta):\n"
                                                        "    print(delta)\n"
                                                        "func _input(event):\n"
                                                        "    print(event)\n"
                                                        "func throw(value):\n"
                                                        "    print(value)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("virtual void _ready() override") != std::string::npos);
    REQUIRE(result.unit.header.find("virtual void _process(double delta) override") !=
            std::string::npos);
    REQUIRE(result.unit.header.find(
                "virtual void _input(const godot::Ref<godot::InputEvent>& event) override") !=
            std::string::npos);
    REQUIRE(result.unit.header.find("throw_(godot::Variant value)") != std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"_ready\"") == std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"_process\"") == std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"_input\"") == std::string::npos);
    REQUIRE(result.unit.source.find("D_METHOD(\"throw\"") != std::string::npos);
}

TEST_CASE("compiler can namespace project native classes by a build identity") {
    gdpp::CompileOptions options;
    options.native_class_suffix = "_0123456789abcdef";
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "player.gd", "extends Node\nclass_name Player\nfunc answer() -> int:\n    return 42\n",
        options);

    REQUIRE(result.success);
    REQUIRE_EQ(result.unit.class_name, std::string{"GDPPNative_Player_0123456789abcdef"});
    REQUIRE(result.unit.header.find("GDCLASS(GDPPNative_Player_0123456789abcdef") !=
            std::string::npos);
}

TEST_CASE("code generation uses the selected Godot API for new object types") {
    gdpp::CompileOptions options;
    options.target_version = gdpp::GodotVersion::v4_7;
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "accessible.gd", "extends Node\nclass_name Accessible\nvar server: AccessibilityServer\n",
        options);

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("godot_cpp/classes/accessibility_server.hpp") !=
            std::string::npos);
    REQUIRE(result.unit.header.find("godot::AccessibilityServer* server{}") != std::string::npos);
}

TEST_CASE("compiler generates typed named and anonymous enum constants") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "state_machine.gd", "extends Node\n"
                            "class_name StateMachine\n"
                            "enum State { IDLE, WALK = 4, RUN = WALK * 2 }\n"
                            "enum { DEFAULT_LIVES = 3, MAX_LIVES = DEFAULT_LIVES + 2 }\n"
                            "@export var state: State = State.RUN\n"
                            "func choose(value: State) -> State:\n"
                            "    return value\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("struct State") != std::string::npos);
    REQUIRE(result.unit.header.find("_gdpp_enum_RUN = 8") != std::string::npos);
    REQUIRE(result.unit.header.find("_gdpp_enum_MAX_LIVES = 5") != std::string::npos);
    REQUIRE(result.unit.header.find("int64_t choose(int64_t value)") != std::string::npos);
    REQUIRE(result.unit.source.find("State::_gdpp_enum_RUN") != std::string::npos);
    REQUIRE(result.unit.source.find("bind_integer_constant") != std::string::npos);
    REQUIRE(result.unit.source.find("IDLE:0,WALK:4,RUN:8") != std::string::npos);
}

TEST_CASE("compiler omits ambiguous native reflection entries for duplicate enum members") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("duplicate_native_enum_members.gd", "extends Node\n"
                                                             "enum First { NONE, A }\n"
                                                             "enum Second { NONE, B }\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("\"NONE\"") == std::string::npos);
    REQUIRE(result.unit.source.find("\"A\"") != std::string::npos);
    REQUIRE(result.unit.source.find("\"B\"") != std::string::npos);
}

TEST_CASE("compiler resolves inherited engine constants and qualified class enums") {
    gdpp::CompileOptions options;
    options.target_version = gdpp::GodotVersion::v4_5;
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("engine_constants.gd",
                                         "extends Camera3D\n"
                                         "func transformed(what: int) -> bool:\n"
                                         "    return what == NOTIFICATION_TRANSFORM_CHANGED\n"
                                         "func go_back(what: int) -> bool:\n"
                                         "    return what == Node.NOTIFICATION_WM_GO_BACK_REQUEST\n"
                                         "func inherited_mode() -> int:\n"
                                         "    return Node.ProcessMode.PROCESS_MODE_INHERIT\n",
                                         options);

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("find_engine_singleton") == std::string::npos);
    REQUIRE(result.unit.source.find("== 2000") != std::string::npos);
    REQUIRE(result.unit.source.find("== 1007") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<int64_t>(0)") != std::string::npos);
}

TEST_CASE("compiler rejects invalid enum declarations and members") {
    const gdpp::Compiler compiler;
    const auto duplicate = compiler.compile("duplicate.gd", "enum State { IDLE, IDLE }\n");
    const auto nonconstant =
        compiler.compile("nonconstant.gd", "var value: int = 2\nenum State { IDLE = value }\n");
    const auto missing =
        compiler.compile("missing.gd", "enum State { IDLE }\nvar state: State = State.MISSING\n");

    REQUIRE(!duplicate.success);
    REQUIRE(!nonconstant.success);
    REQUIRE(!missing.success);
    REQUIRE(duplicate.unit.header.empty());
}

TEST_CASE("compiler generates single-evaluation match control flow") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("matcher.gd", "extends Node\n"
                                       "enum State { IDLE, WALK = 4, RUN = 8 }\n"
                                       "func classify(value: int) -> String:\n"
                                       "    match value:\n"
                                       "        State.IDLE, State.WALK:\n"
                                       "            return \"slow\"\n"
                                       "        var captured when captured == State.RUN:\n"
                                       "            return \"run\"\n"
                                       "        _:\n"
                                       "            return \"unknown\"\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("const auto _gdpp_match_value_0 = value") != std::string::npos);
    REQUIRE(result.unit.source.find("bool _gdpp_match_done_0 = false") != std::string::npos);
    REQUIRE(result.unit.source.find("State::_gdpp_enum_IDLE") != std::string::npos);
    REQUIRE(result.unit.source.find("auto captured = _gdpp_match_value_0") != std::string::npos);
    REQUIRE(
        result.unit.source.find("if (static_cast<bool>((captured == State::_gdpp_enum_RUN)))") !=
        std::string::npos);
    REQUIRE(result.unit.source.find("    return {};\n}") != std::string::npos);
}

TEST_CASE("compiler rejects nonconstant and unreachable match patterns") {
    const gdpp::Compiler compiler;
    const auto nonconstant =
        compiler.compile("nonconstant_match.gd", "func test(value: int) -> int:\n"
                                                 "    match value:\n"
                                                 "        value:\n"
                                                 "            return 1\n"
                                                 "        _:\n"
                                                 "            return 0\n");
    const auto unreachable =
        compiler.compile("unreachable_match.gd", "func test(value: int) -> int:\n"
                                                 "    match value:\n"
                                                 "        _:\n"
                                                 "            return 0\n"
                                                 "        1:\n"
                                                 "            return 1\n");

    REQUIRE(!nonconstant.success);
    REQUIRE(!unreachable.success);
    REQUIRE(nonconstant.unit.source.empty());
}

TEST_CASE("dynamic match values use the Variant compatibility runtime") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("dynamic_match.gd", "func classify(value: Variant) -> String:\n"
                                             "    match value:\n"
                                             "        \"ready\":\n"
                                             "            return \"ok\"\n"
                                             "        _:\n"
                                             "            return \"other\"\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::binary(godot::Variant::OP_EQUAL") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<bool>") != std::string::npos);
}

TEST_CASE("compiler lowers dynamic calls properties and keyed access through the runtime") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "dynamic_access.gd",
        "func invoke(target: Variant, value: int) -> Variant:\n"
        "    return target.answer(value, value + 1)\n"
        "func read_property(target: Variant) -> Variant:\n"
        "    return target.score\n"
        "func write_property(target: Variant, value: Variant) -> void:\n"
        "    target.score = value\n"
        "func increment_property(target: Variant, value: Variant) -> void:\n"
        "    target.score += value\n"
        "func read_key(target: Variant, key: Variant) -> Variant:\n"
        "    return target[key]\n"
        "func write_key(target: Variant, key: Variant, value: Variant) -> void:\n"
        "    target[key] = value\n"
        "func increment_key(target: Variant, key: Variant, value: Variant) -> void:\n"
        "    target[key] += value\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::call_dynamic") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::StringName(\"answer\")") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::get_named") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::set_named") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::StringName(\"score\")") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::get_key") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::set_key") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_dynamic_current_") != std::string::npos);
    REQUIRE(result.unit.source.find("target.answer(") == std::string::npos);
    REQUIRE(result.unit.source.find("target.score") == std::string::npos);
}

TEST_CASE("compiler lowers Dictionary named access through its keyed native ABI") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("dictionary_named_access.gd",
                         "func mutate(values: Dictionary, increment: int) -> Variant:\n"
                         "    values.score += increment\n"
                         "    values.label = \"ready\"\n"
                         "    return values.score\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("_gdpp_dictionary_target_") != std::string::npos);
    REQUIRE(result.unit.source.find("static const godot::Variant _gdpp_dictionary_read_key_") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant &_gdpp_dictionary_slot_") != std::string::npos);
    REQUIRE(result.unit.source.find("[_gdpp_dictionary_key_") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::get_named") == std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::set_named") == std::string::npos);
}

TEST_CASE("dynamic nested value assignments write every changed value back to its owner") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("dynamic_nested_assignment.gd",
                         "extends Node\n"
                         "var local_variant: Variant = Vector2.ZERO\n"
                         "func mutate(target: Variant, records: Array, index: int) -> void:\n"
                         "    target.position.y -= 1.0\n"
                         "    target.transform.origin.x = 4.0\n"
                         "    local_variant.x = 3.0\n"
                         "    records[index].tint.a = 0.25\n");

    REQUIRE(result.success);
    const auto& source = result.unit.source;

    // Node2D.position is a Vector2 copy. The component update must be followed by a write of the
    // changed Vector2 into position, and a Variant root must then be stored back as well.
    REQUIRE(source.find("godot::StringName(\"y\")") != std::string::npos);
    REQUIRE(source.find("godot::StringName(\"position\"), _gdpp_dynamic_child_") !=
            std::string::npos);
    REQUIRE(source.find("target = _gdpp_dynamic_root_") != std::string::npos);

    // Deeper Transform3D.origin.x-style chains must reverse through every value layer.
    REQUIRE(source.find("godot::StringName(\"origin\"), _gdpp_dynamic_child_") !=
            std::string::npos);
    REQUIRE(source.find("godot::StringName(\"transform\"), _gdpp_dynamic_child_") !=
            std::string::npos);

    // Variant-held value roots and Array[index] records exercise the two distinct final stores.
    REQUIRE(source.find("local_variant = _gdpp_dynamic_root_") != std::string::npos);
    REQUIRE(source.find("gdpp::runtime::set_key(_gdpp_dynamic_root_") != std::string::npos);
    REQUIRE(source.find("godot::StringName(\"tint\"), _gdpp_dynamic_child_") != std::string::npos);
}

TEST_CASE("plain script variables remain dynamically visible on generated native objects") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("plain_dynamic_property.gd",
                                         "extends Node\n"
                                         "var open_bag: bool = false\n"
                                         "func read_other(target: Variant) -> Variant:\n"
                                         "    return target.open_bag\n"
                                         "func write_other(target: Variant, value: bool) -> void:\n"
                                         "    target.open_bag = value\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("_gdpp_get_open_bag") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_set_open_bag") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_USAGE_SCRIPT_VARIABLE") != std::string::npos);
}

TEST_CASE("compiler lowers Variant iteration and Callable calls with ordered temporaries") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "dynamic_protocols.gd", "func sum(values: Variant) -> int:\n"
                                "    var total: int = 0\n"
                                "    for value in values:\n"
                                "        if value == 2:\n"
                                "            continue\n"
                                "        total += value\n"
                                "    return total\n"
                                "func invoke(callback: Callable, value: int) -> Variant:\n"
                                "    return callback.call(value)\n"
                                "func invoke_dynamic(callback: Variant, value: int) -> Variant:\n"
                                "    return callback.call(value)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_init") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_next") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::iter_get") != std::string::npos);
    REQUIRE(result.unit.source.find("for (bool _gdpp_dynamic_available_") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant value =") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_callable_argument_") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::call_dynamic") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::StringName(\"call\")") != std::string::npos);
}

TEST_CASE("compiler retains local lambda adapters for direct native calls") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile(
        "local_callable.gd", "func invoke(value: int) -> int:\n"
                             "    var operation := func(item: int) -> int: return item * 3 + 1\n"
                             "    return operation.call(value)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("auto operation = gdpp::runtime::make_local_callable(") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("[=](const auto &_gdpp_lambda_arguments_") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::Callable operation") == std::string::npos);
}

TEST_CASE("compiler rejects direct Callable and unknown expression invocation") {
    const gdpp::Compiler compiler;
    const auto direct =
        compiler.compile("direct_callable.gd", "func invoke(callback: Callable) -> Variant:\n"
                                               "    return callback()\n");
    const auto unknown = compiler.compile("unknown_call.gd", "func invoke() -> Variant:\n"
                                                             "    return missing_function()\n");
    const auto expression =
        compiler.compile("expression_call.gd", "func invoke() -> Variant:\n"
                                               "    return [Callable()][0]()\n");

    REQUIRE(!direct.success);
    REQUIRE(!unknown.success);
    REQUIRE(!expression.success);
    bool found_direct = false;
    for (const auto& diagnostic : direct.diagnostics)
        found_direct = found_direct || diagnostic.code == "GDS4070";
    bool found_unknown = false;
    for (const auto& diagnostic : unknown.diagnostics)
        found_unknown = found_unknown || diagnostic.code == "GDS4071";
    bool found_expression = false;
    for (const auto& diagnostic : expression.diagnostics)
        found_expression = found_expression || diagnostic.code == "GDS4072";
    REQUIRE(found_direct);
    REQUIRE(found_unknown);
    REQUIRE(found_expression);
}

TEST_CASE("compiler generates nonrecursive Godot 4 property accessors") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("health.gd", "extends Node\n"
                                                      "@export var health: int = 100:\n"
                                                      "    set(value):\n"
                                                      "        health = value\n"
                                                      "    get:\n"
                                                      "        return health\n"
                                                      "var internal_state: int = 1:\n"
                                                      "    get:\n"
                                                      "        return internal_state\n"
                                                      "func update(value: int) -> int:\n"
                                                      "    health = value\n"
                                                      "    return health\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("int64_t _gdpp_get_health()") != std::string::npos);
    REQUIRE(result.unit.source.find("int64_t GDPPNative_Health::_gdpp_get_health()") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("health = value;") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_set_health(value);") != std::string::npos);
    REQUIRE(result.unit.source.find("return _gdpp_get_health();") != std::string::npos);
    REQUIRE(result.unit.source.find("return health;\n    return health;") == std::string::npos);
    REQUIRE(result.unit.source.find("return _gdpp_get_health();\n    return {};") ==
            std::string::npos);
    REQUIRE(result.unit.source.find("godot::PROPERTY_USAGE_SCRIPT_VARIABLE") != std::string::npos);
}

TEST_CASE("compiler generates validated method-bound property accessors") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("bound.gd", "extends Node\n"
                                     "var active: bool = true: set = set_active, get = is_active\n"
                                     "func set_active(value: bool) -> void:\n"
                                     "    active = value\n"
                                     "func is_active() -> bool:\n"
                                     "    return active\n"
                                     "func disable() -> bool:\n"
                                     "    active = false\n"
                                     "    return active\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("return is_active();") != std::string::npos);
    REQUIRE(result.unit.source.find("set_active(value);") != std::string::npos);
    REQUIRE(result.unit.source.find("active = value;") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_set_active(false);") != std::string::npos);
    REQUIRE(result.unit.source.find("return _gdpp_get_active();") != std::string::npos);
}

TEST_CASE("compiler rejects invalid bound property accessor signatures") {
    const gdpp::Compiler compiler;
    const auto missing = compiler.compile("missing.gd", "var value: int: get = missing_getter\n");
    const auto setter_arity = compiler.compile("arity.gd", "var value: int: set = set_value\n"
                                                           "func set_value() -> void:\n"
                                                           "    pass\n");
    const auto getter_arity =
        compiler.compile("getter_arity.gd", "var value: int: get = get_value\n"
                                            "func get_value(extra: int) -> int:\n"
                                            "    return extra\n");
    const auto static_mismatch =
        compiler.compile("static.gd", "static var value: int: set = set_value\n"
                                      "func set_value(next: int) -> void:\n"
                                      "    value = next\n");

    REQUIRE(!missing.success);
    REQUIRE(!setter_arity.success);
    REQUIRE(!getter_arity.success);
    REQUIRE(!static_mismatch.success);
}

TEST_CASE("compiler rejects malformed property accessors") {
    const gdpp::Compiler compiler;
    const auto missing_return =
        compiler.compile("getter.gd", "var value: int:\n    get:\n        pass\n");
    const auto invalid_setter_return =
        compiler.compile("setter.gd", "var value: int:\n    set(next):\n        return next\n");

    REQUIRE(!missing_return.success);
    REQUIRE(!invalid_setter_return.success);
    REQUIRE(missing_return.unit.header.empty());
}

TEST_CASE("compiler returns structured errors instead of partial output") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("broken.gd", "const answer\n");

    REQUIRE(!result.success);
    REQUIRE(!result.diagnostics.empty());
    REQUIRE(result.unit.header.empty());
}

TEST_CASE("compiler generates object and builtin type tests") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("type_tests.gd", "extends Node\n"
                                          "func is_node(value: Variant) -> bool:\n"
                                          "    return value is Node\n"
                                          "func is_string(value: Variant) -> bool:\n"
                                          "    return value is String\n"
                                          "func is_integer(value: int) -> bool:\n"
                                          "    return value is int\n"
                                          "func integer_is_node(value: int) -> bool:\n"
                                          "    return value is Node\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Object::cast_to<godot::Node>("
                                    "(value).get_validated_object()) != nullptr") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("(value).get_type() == godot::Variant::STRING") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<void>(value), true") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<void>(value), false") != std::string::npos);
}

TEST_CASE("compiler lowers negated type tests and checked object downcasts") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("downcast.gd", "extends Node\n"
                                                        "func node_2d(value: Node) -> Node2D:\n"
                                                        "    return value\n"
                                                        "func differs(value: Node) -> bool:\n"
                                                        "    return value is not Node2D\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Object::cast_to<godot::Node2D>(value)") !=
            std::string::npos);
    REQUIRE(
        result.unit.source.find("!((godot::Object::cast_to<godot::Node2D>(value) != nullptr))") !=
        std::string::npos);
}

TEST_CASE("typed scene nodes retain dynamic script dispatch for unknown members") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("scene_dispatch.gd", "extends Node\n"
                                              "func start(screen: Node2D) -> Variant:\n"
                                              "    screen.enabled = true\n"
                                              "    return screen.initialize()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::set_named") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::call_dynamic") != std::string::npos);
}

TEST_CASE("compiler rejects invalid type-test operands transactionally") {
    const gdpp::Compiler compiler;
    const auto invalid_target =
        compiler.compile("invalid_target.gd", "func accepts(value: Variant) -> bool:\n"
                                              "    return value is 42\n");
    const auto invalid_value = compiler.compile("invalid_value.gd", "func accepts() -> bool:\n"
                                                                    "    return Node is Node\n");

    REQUIRE(!invalid_target.success);
    REQUIRE(!invalid_value.success);
    REQUIRE(invalid_target.unit.header.empty());
    REQUIRE(invalid_target.unit.source.empty());
    REQUIRE(invalid_value.unit.header.empty());
    REQUIRE(invalid_value.unit.source.empty());
    bool found_target_diagnostic = false;
    for (const auto& diagnostic : invalid_target.diagnostics)
        found_target_diagnostic = found_target_diagnostic || diagnostic.code == "GDS4067";
    bool found_value_diagnostic = false;
    for (const auto& diagnostic : invalid_value.diagnostics)
        found_value_diagnostic = found_value_diagnostic || diagnostic.code == "GDS4068";
    REQUIRE(found_target_diagnostic);
    REQUIRE(found_value_diagnostic);
}

TEST_CASE("semantic flow analysis requires concrete returns on every reachable path") {
    const gdpp::Compiler compiler;
    const auto exhaustive_if =
        compiler.compile("if_returns.gd", "func choose(value: bool) -> int:\n"
                                          "    if value:\n"
                                          "        return 1\n"
                                          "    else:\n"
                                          "        return 2\n");
    const auto exhaustive_match =
        compiler.compile("match_returns.gd", "func choose(value: int) -> String:\n"
                                             "    match value:\n"
                                             "        0:\n"
                                             "            return \"zero\"\n"
                                             "        _:\n"
                                             "            return \"other\"\n");
    const auto infinite_loop = compiler.compile("infinite.gd", "func run_forever() -> int:\n"
                                                               "    while true:\n"
                                                               "        pass\n");
    const auto partial_if = compiler.compile("partial.gd", "func choose(value: bool) -> int:\n"
                                                           "    if value:\n"
                                                           "        return 1\n");

    REQUIRE(exhaustive_if.success);
    REQUIRE(exhaustive_match.success);
    REQUIRE(infinite_loop.success);
    REQUIRE(!partial_if.success);
    bool found_missing_return = false;
    for (const auto& diagnostic : partial_if.diagnostics)
        found_missing_return = found_missing_return || diagnostic.code == "GDS4009";
    REQUIRE(found_missing_return);
}

TEST_CASE("semantic flow analysis rejects unreachable statements and partial getters") {
    const gdpp::Compiler compiler;
    const auto unreachable = compiler.compile("unreachable.gd", "func answer() -> int:\n"
                                                                "    return 42\n"
                                                                "    print(\"never\")\n");
    const auto complete_getter = compiler.compile("complete_getter.gd", "var enabled: bool = true\n"
                                                                        "var value: int:\n"
                                                                        "    get:\n"
                                                                        "        if enabled:\n"
                                                                        "            return 1\n"
                                                                        "        else:\n"
                                                                        "            return 2\n");
    const auto partial_getter = compiler.compile("partial_getter.gd", "var enabled: bool = true\n"
                                                                      "var value: int:\n"
                                                                      "    get:\n"
                                                                      "        if enabled:\n"
                                                                      "            return 1\n");

    REQUIRE(!unreachable.success);
    REQUIRE(complete_getter.success);
    REQUIRE(!partial_getter.success);
    bool found_unreachable = false;
    for (const auto& diagnostic : unreachable.diagnostics)
        found_unreachable = found_unreachable || diagnostic.code == "GDS4069";
    bool found_partial_getter = false;
    for (const auto& diagnostic : partial_getter.diagnostics)
        found_partial_getter = found_partial_getter || diagnostic.code == "GDS4050";
    REQUIRE(found_unreachable);
    REQUIRE(found_partial_getter);
}

TEST_CASE("Godot numeric class names map to their actual header names") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("sprite.gd", "extends Node2D\nclass_name SpriteActor\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("#include <godot_cpp/classes/node2d.hpp>") !=
            std::string::npos);
}

TEST_CASE("semantic analysis rejects duplicate declarations and invalid typed initializers") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid.gd", "var count: int = \"not a number\"\n"
                                                       "var count: int = 2\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
    REQUIRE(result.diagnostics.size() >= std::size_t{2});
}

TEST_CASE("semantic analysis rejects constant writes and loop control outside loops") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid_flow.gd", "const LIMIT := 10\n"
                                                            "func mutate() -> void:\n"
                                                            "    LIMIT = 11\n"
                                                            "    break\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.header.empty());
}

TEST_CASE("semantic analysis rejects every non-lvalue assignment target") {
    const gdpp::Compiler compiler;
    const auto literal = compiler.compile("literal_target.gd", "func bad() -> void:\n    1 = 2\n");
    const auto call = compiler.compile(
        "call_target.gd",
        "func value() -> int:\n    return 1\nfunc bad() -> void:\n    value() = 2\n");

    REQUIRE(!literal.success);
    REQUIRE(!call.success);
    REQUIRE(literal.unit.source.empty());
    REQUIRE(call.unit.source.empty());
}

TEST_CASE("inferred collection types and numeric-looking strings generate native types") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("collections.gd", "extends Node\n"
                                           "var count := 1\n"
                                           "var text := \"123\"\n"
                                           "func visit() -> void:\n"
                                           "    var values := [1, 2]\n"
                                           "    var labels := {\"one\": 1}\n"
                                           "    for value in values:\n"
                                           "        print(labels[\"one\"], value)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("count = static_cast<int64_t>(1)") != std::string::npos);
    REQUIRE(result.unit.source.find("text = godot::String(\"123\")") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Array values") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_dictionary_") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant value = gdpp::runtime::iter_get") !=
            std::string::npos);
}

TEST_CASE("empty strings remain empty through lexing semantic analysis and code generation") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("empty.gd", "var text := \"\"\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("text = godot::String(\"\")") != std::string::npos);
}

TEST_CASE("compiler optimization can be enabled or disabled without changing the frontend") {
    const gdpp::Compiler compiler;
    const std::string source = "func answer() -> int:\n    return 40 + 2\n";
    const auto optimized = compiler.compile("fold.gd", source);
    gdpp::CompileOptions debug_options;
    debug_options.optimize = false;
    const auto unoptimized = compiler.compile("fold.gd", source, debug_options);

    REQUIRE(optimized.success);
    REQUIRE(unoptimized.success);
    REQUIRE_EQ(optimized.optimization.constants_folded, std::size_t{1});
    REQUIRE_EQ(unoptimized.optimization.constants_folded, std::size_t{0});
    REQUIRE(optimized.unit.source.find("return static_cast<int64_t>(42);") != std::string::npos);
    REQUIRE(unoptimized.unit.source.find("return (static_cast<int64_t>(40) + "
                                         "static_cast<int64_t>(2));") != std::string::npos);
}

TEST_CASE("Godot API inheritance resolves native methods properties and builtin types") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("mover.gd", "extends Node2D\n"
                                                     "func move_by(delta: Vector2) -> void:\n"
                                                     "    position += delta\n"
                                                     "    queue_redraw()\n"
                                                     "func magnitude(value: Vector2) -> float:\n"
                                                     "    return value.length()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("#include <godot_cpp/variant/vector2.hpp>") !=
            std::string::npos);
    REQUIRE(result.unit.header.find("godot::Vector2 delta") != std::string::npos);
    REQUIRE(result.unit.source.find("set_position((get_position() + delta))") != std::string::npos);
    REQUIRE(result.unit.source.find("queue_redraw()") != std::string::npos);
    REQUIRE(result.unit.source.find(".length()") != std::string::npos);
    REQUIRE(result.unit.source.find("_gdpp_call_receiver_") != std::string::npos);
}

TEST_CASE("builtin unary operators use Variant evaluation when godot-cpp has no operator") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("unary_builtin.gd", "extends Node\n"
                                             "func positive(value: Vector2) -> Vector2:\n"
                                             "    return +value\n"
                                             "func negative(value: Vector2) -> Vector2:\n"
                                             "    return -value\n"
                                             "func integer(value: int) -> int:\n"
                                             "    return +value\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("godot::Variant::OP_POSITIVE") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Variant::OP_NEGATE") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<godot::Vector2>(godot::Variant(") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("return (+value);") != std::string::npos);
}

TEST_CASE("static function values use owner-free callables with default argument semantics") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("static_callable.gd", "extends Resource\n"
                                               "static func compare(left, right = 0) -> bool:\n"
                                               "    return left < right\n"
                                               "static func sorter(values: Array) -> void:\n"
                                               "    values.sort_custom(compare)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("gdpp::runtime::make_callable(nullptr, 1, 2") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("GDPPNative_StaticCallable::compare(") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Callable(this, godot::StringName(\"compare\"))") ==
            std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::default_argument()") != std::string::npos);
}

TEST_CASE("unknown lowercase identifiers fail before native code generation") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("typo.gd", "extends Node\n"
                                                    "func broken() -> void:\n"
                                                    "    print(misspelled_value)\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
    REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "GDS4122"; }));
}

TEST_CASE("expression statements explicitly discard native results") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("discard.gd", "extends Node\n"
                                                       "func consume(value: float) -> void:\n"
                                                       "    int(value)\n"
                                                       "    Vector2(1, 2)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("static_cast<void>(static_cast<int64_t>") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<void>(([&]() -> godot::Vector2") !=
            std::string::npos);
}

TEST_CASE("Godot API method arity errors stop native code generation") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid_api.gd", "extends Node\n"
                                                           "func invalid() -> void:\n"
                                                           "    add_child()\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
    REQUIRE(!result.diagnostics.empty());
}

TEST_CASE("Godot API argument type errors are diagnosed before C++ generation") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid_api_type.gd", "extends Node\n"
                                                                "func invalid() -> void:\n"
                                                                "    add_child(42)\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.header.empty());
}

TEST_CASE("compiler gates Godot 4.7 APIs using the selected target") {
    const std::string source = "extends Node\n"
                               "func hdr_enabled() -> bool:\n"
                               "    return DisplayServer.window_is_hdr_output_enabled()\n";
    const gdpp::Compiler compiler;
    const auto baseline = compiler.compile("versioned.gd", source);
    gdpp::CompileOptions latest_options;
    latest_options.target_version = gdpp::GodotVersion::v4_7;
    const auto latest = compiler.compile("versioned.gd", source, latest_options);

    REQUIRE(!baseline.success);
    REQUIRE(latest.success);
    REQUIRE(latest.unit.source.find("window_is_hdr_output_enabled") != std::string::npos);
}

TEST_CASE("typed Godot object parameters generate pointer calls") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("objects.gd", "extends Node\n"
                                                       "func release(node: Node) -> void:\n"
                                                       "    node.queue_free()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("godot::Node* node") != std::string::npos);
    REQUIRE(result.unit.source.find("->queue_free()") != std::string::npos);
}

TEST_CASE("Godot builtin constructors resolve overloads and native value types") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("vectors.gd", "extends Node\n"
                                                       "var origin := Vector2(1.0, 2.0)\n"
                                                       "var accent := Color(0.1, 0.2, 0.3, 1.0)\n"
                                                       "func distance(value: Vector2) -> float:\n"
                                                       "    return value.distance_to(origin)\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("return godot::Vector2(static_cast<godot::real_t>(") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<godot::real_t>(_gdpp_call_argument_") !=
            std::string::npos);
    REQUIRE(result.unit.source.find(".distance_to(") != std::string::npos);
}

TEST_CASE("invalid Godot builtin constructor overloads are diagnosed") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid_vector.gd", "var value := Vector2(\"x\")\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
}

TEST_CASE("Godot singleton calls lower to native get_singleton access") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("input_reader.gd", "extends Node\n"
                                            "func pressed() -> bool:\n"
                                            "    return Input.is_action_pressed(\"ui_accept\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.header.find("#include <godot_cpp/classes/input.hpp>") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::Input::get_singleton()") != std::string::npos);
    REQUIRE(result.unit.source.find("->is_action_pressed(") != std::string::npos);
}

TEST_CASE("Godot scalar API returns cross the native ABI with explicit conversions") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("native_returns.gd", "extends Node\n"
                                              "func ticks() -> int:\n"
                                              "    return Time.get_ticks_usec()\n"
                                              "func random_value() -> int:\n"
                                              "    return randi()\n"
                                              "func screen_index() -> int:\n"
                                              "    return get_window().current_screen\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("->get_ticks_usec())") != std::string::npos);
    REQUIRE(result.unit.source.find("static_cast<int64_t>(godot::UtilityFunctions::randi())") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("->get_current_screen())") != std::string::npos);
}

TEST_CASE("third-party GDExtension singletons resolve through Engine at runtime") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("steam.gd", "extends Node\n"
                                                     "func poll() -> void:\n"
                                                     "    Steam.run_callbacks()\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find(
                "gdpp::runtime::find_engine_singleton(godot::StringName(\"Steam\"))") !=
            std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::call_dynamic") != std::string::npos);
    REQUIRE(result.unit.source.find("static const godot::StringName _gdpp_dynamic_method_") !=
            std::string::npos);
}

TEST_CASE("Godot builtin static methods lower to native scope calls") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("colors.gd", "extends Node\n"
                                                      "var accent := Color.html(\"ff8800\")\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("return godot::Color::html(_gdpp_call_argument_") !=
            std::string::npos);
}

TEST_CASE("compiler lowers Godot 4.7 global utilities constants enums and range") {
    gdpp::CompileOptions options;
    options.target_version = gdpp::GodotVersion::v4_7;
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("globals.gd",
                                         "extends Node\n"
                                         "var angle: float = deg_to_rad(180.0)\n"
                                         "var side: Side = Side.SIDE_LEFT\n"
                                         "var maximum: int = INT64_MAX\n"
                                         "var circle: float = PI * 2.0\n"
                                         "func label(value: float) -> String:\n"
                                         "    return str(clampf(value, 0.0, 1.0))\n"
                                         "func indices() -> Array:\n"
                                         "    return range(1, 8, 2)\n",
                                         options);

    REQUIRE(result.success);
    REQUIRE(
        result.unit.source.find("godot::UtilityFunctions::deg_to_rad(_gdpp_utility_argument_") !=
        std::string::npos);
    REQUIRE(result.unit.source.find("side = 0") != std::string::npos);
    REQUIRE(result.unit.source.find("9223372036854775807") != std::string::npos);
    REQUIRE(result.unit.source.find("Math_PI") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::UtilityFunctions::clampf") != std::string::npos);
    REQUIRE(result.unit.source.find("godot::UtilityFunctions::str") != std::string::npos);
    REQUIRE(result.unit.source.find("gdpp::runtime::make_range") != std::string::npos);
}

TEST_CASE("dynamic logical operators short circuit and utility arguments keep source order") {
    const gdpp::Compiler compiler;
    const auto result =
        compiler.compile("evaluation_order.gd", "extends Node\n"
                                                "func probe(value: int) -> int:\n"
                                                "    return value\n"
                                                "func dynamic_and(left: Variant) -> bool:\n"
                                                "    return left and probe(1)\n"
                                                "func ordered() -> Variant:\n"
                                                "    return max(probe(1), probe(2))\n");

    REQUIRE(result.success);
    REQUIRE(result.unit.source.find("_gdpp_logic_left_") != std::string::npos);
    REQUIRE(result.unit.source.find("if (!static_cast<bool>") != std::string::npos);
    const auto first = result.unit.source.find("_gdpp_utility_argument_", 0);
    REQUIRE(first != std::string::npos);
    const auto second = result.unit.source.find("_gdpp_utility_argument_", first + 1);
    REQUIRE(second != std::string::npos);
    REQUIRE(first < second);
    REQUIRE(result.unit.source.find(" = static_cast<int64_t>(1);", first) != std::string::npos);
    REQUIRE(result.unit.source.find(" = static_cast<int64_t>(2);", second) != std::string::npos);
}

TEST_CASE("instance Godot methods cannot be called through type references") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("invalid_static.gd", "extends Node\n"
                                                              "func invalid() -> void:\n"
                                                              "    Vector2.length()\n");

    REQUIRE(!result.success);
    REQUIRE(result.unit.source.empty());
}

TEST_CASE("compiler reports deterministic stage and IR size metrics") {
    const gdpp::Compiler compiler;
    const auto result = compiler.compile("metrics.gd", "extends Node\n"
                                                       "func sum(limit: int) -> int:\n"
                                                       "    var total := 0\n"
                                                       "    for value in range(limit):\n"
                                                       "        total += value\n"
                                                       "    return total\n");

    REQUIRE(result.success);
    REQUIRE(result.metrics.total_ns > 0);
    REQUIRE(result.metrics.lex_ns > 0);
    REQUIRE(result.metrics.parse_ns > 0);
    REQUIRE(result.metrics.token_count > 10);
    REQUIRE(result.metrics.ast_expression_count > 0);
    REQUIRE(result.metrics.ast_statement_count >= std::size_t{4});
    REQUIRE(result.metrics.hir_expression_count > 0);
    REQUIRE(result.metrics.hir_statement_count >= std::size_t{4});
    REQUIRE(result.metrics.mir_function_count == std::size_t{1});
    REQUIRE(result.metrics.mir_block_count >= std::size_t{4});
    REQUIRE(result.metrics.mir_instruction_count > 0);
}
