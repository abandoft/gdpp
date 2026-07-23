#include "support/test.hpp"

#include "gdpp/semantic/script_symbols.hpp"

#include <algorithm>
#include <string>

namespace {

gdpp::ScriptMemberSymbol method(std::string name) {
    gdpp::ScriptMemberSymbol result;
    result.kind = gdpp::ScriptMemberKind::function;
    result.name = std::move(name);
    result.type = {gdpp::TypeKind::integer, "int"};
    return result;
}

TEST_CASE("attached script symbols inherit external ClassDB contracts") {
    gdpp::ScriptSymbolTable symbols;

    gdpp::ExternalClassSymbol vendor;
    vendor.name = "VendorCharacter";
    vendor.godot_base_type = "Node3D";
    vendor.provider_abi = "vendor:7";
    vendor.members_complete = true;
    vendor.members.push_back(method("vendor_tick"));
    gdpp::ScriptMemberSymbol property;
    property.kind = gdpp::ScriptMemberKind::field;
    property.name = "stamina";
    property.type = {gdpp::TypeKind::floating, "float"};
    vendor.members.push_back(property);
    gdpp::ScriptEnumSymbol state;
    state.name = "State";
    state.entries.push_back({"IDLE", 0});
    vendor.enums.push_back(state);
    symbols.add_external(std::move(vendor));

    gdpp::ScriptClassSymbol root;
    root.path = "player.gd";
    root.script_name = "Player";
    root.native_class_name = "GDPPNative_Player";
    root.header_file_name = "player.gd.hpp";
    root.godot_base_type = "Node3D";
    root.external_base_name = "VendorCharacter";
    root.attached = true;
    root.members.push_back(method("move_player"));
    symbols.add(std::move(root));

    gdpp::ScriptClassSymbol child;
    child.path = "elite_player.gd";
    child.script_name = "ElitePlayer";
    child.native_class_name = "GDPPNative_ElitePlayer";
    child.header_file_name = "elite_player.gd.hpp";
    child.godot_base_type = "Node3D";
    child.base_script_path = "player.gd";
    child.attached = true;
    symbols.add(std::move(child));

    const auto* derived = symbols.find_path("elite_player.gd");
    REQUIRE(derived != nullptr);
    REQUIRE(symbols.external_base_of(*derived) != nullptr);
    REQUIRE(symbols.find_member(*derived, "vendor_tick") != nullptr);
    REQUIRE(symbols.find_member(*derived, "stamina") != nullptr);
    REQUIRE(symbols.find_enum(*derived, "State") != nullptr);
    REQUIRE(!symbols.requires_dynamic_dispatch(*derived, "move_player"));

    const auto inherited = symbols.inherited_members(*derived);
    REQUIRE(std::any_of(inherited.begin(), inherited.end(),
                        [](const auto* item) { return item->name == "move_player"; }));
    REQUIRE(std::any_of(inherited.begin(), inherited.end(),
                        [](const auto* item) { return item->name == "vendor_tick"; }));
    REQUIRE(std::any_of(inherited.begin(), inherited.end(),
                        [](const auto* item) { return item->name == "stamina"; }));
}

TEST_CASE("attached script declarations shadow external members") {
    gdpp::ScriptSymbolTable symbols;
    gdpp::ExternalClassSymbol vendor;
    vendor.name = "VendorNode";
    vendor.members.push_back(method("run"));
    symbols.add_external(std::move(vendor));

    gdpp::ScriptClassSymbol script;
    script.path = "runner.gd";
    script.script_name = "Runner";
    script.native_class_name = "GDPPNative_Runner";
    script.header_file_name = "runner.gd.hpp";
    script.external_base_name = "VendorNode";
    script.attached = true;
    auto override = method("run");
    override.type = {gdpp::TypeKind::string, "String"};
    script.members.push_back(override);
    symbols.add(std::move(script));

    const auto* owner = symbols.find_path("runner.gd");
    REQUIRE(owner != nullptr);
    const auto* resolved = symbols.find_member(*owner, "run");
    REQUIRE(resolved != nullptr);
    REQUIRE_EQ(resolved->type, override.type);
}

TEST_CASE("script symbols resolve project-wide internal native identities") {
    gdpp::ScriptSymbolTable symbols;
    gdpp::ScriptClassSymbol script;
    script.path = "messages.gd";
    script.script_name = "messages";
    script.native_class_name = "GDPPNative_Messages";
    script.header_file_name = "messages.gd.hpp";
    gdpp::ScriptInnerClassSymbol message;
    message.name = "Message";
    message.native_class_name = "GDPPNative_Messages__Message";
    script.inner_classes.push_back(message);
    symbols.add(std::move(script));

    const auto* inner = symbols.find_inner_native("GDPPNative_Messages__Message");
    REQUIRE(inner != nullptr);
    REQUIRE_EQ(inner->name, std::string{"Message"});
    const auto* owner = symbols.owner_of(*inner);
    REQUIRE(owner != nullptr);
    REQUIRE_EQ(owner->path, std::string{"messages.gd"});
    REQUIRE(symbols.find_inner_native("Message") == nullptr);

    gdpp::ScriptClassSymbol consumer;
    consumer.path = "consumer.gd";
    consumer.script_name = "Consumer";
    consumer.native_class_name = "GDPPNative_Consumer";
    consumer.header_file_name = "consumer.gd.hpp";
    gdpp::ScriptMemberSymbol value;
    value.name = "value";
    value.type = {gdpp::TypeKind::object, "GDPPNative_Messages__Message"};
    value.parameters.push_back({gdpp::TypeKind::array, "Array[GDPPNative_Messages__Message]"});
    consumer.members.push_back(value);
    symbols.add(std::move(consumer));

    symbols.update_class_identity("messages.gd", "GDPPNative_Messages_v2", "messages_v2.gd.hpp");
    REQUIRE(symbols.find_native_class("GDPPNative_Messages") == nullptr);
    REQUIRE(symbols.find_inner_native("GDPPNative_Messages__Message") == nullptr);
    REQUIRE(symbols.find_inner_native("GDPPNative_Messages_v2__Message") != nullptr);
    const auto* updated_consumer = symbols.find_path("consumer.gd");
    REQUIRE(updated_consumer != nullptr);
    const auto* updated_value = symbols.find_member(*updated_consumer, "value");
    REQUIRE(updated_value != nullptr);
    REQUIRE_EQ(updated_value->type.name, std::string{"GDPPNative_Messages_v2__Message"});
    REQUIRE_EQ(updated_value->parameters.front().name,
               std::string{"Array[GDPPNative_Messages_v2__Message]"});
}

TEST_CASE("script symbols traverse internal inheritance and classify dynamic dispatch") {
    gdpp::ScriptSymbolTable symbols;
    gdpp::ScriptClassSymbol script;
    script.path = "strategies.gd";
    script.script_name = "Strategies";
    script.native_class_name = "GDPPNative_Strategies";
    script.header_file_name = "strategies.gd.hpp";

    gdpp::ScriptInnerClassSymbol base;
    base.name = "Base";
    base.native_class_name = "GDPPNative_Strategies__Base";
    auto transform = method("transform");
    transform.parameters.push_back({gdpp::TypeKind::object, "Node"});
    base.members.push_back(transform);

    gdpp::ScriptInnerClassSymbol compatible;
    compatible.name = "Compatible";
    compatible.native_class_name = "GDPPNative_Strategies__Compatible";
    compatible.base_class_name = "Base";
    compatible.members.push_back(transform);

    gdpp::ScriptInnerClassSymbol coroutine;
    coroutine.name = "Coroutine";
    coroutine.native_class_name = "GDPPNative_Strategies__Coroutine";
    coroutine.base_class_name = "Compatible";
    auto async_transform = transform;
    async_transform.is_coroutine = true;
    coroutine.members.push_back(async_transform);

    script.inner_classes.push_back(std::move(base));
    script.inner_classes.push_back(std::move(compatible));
    script.inner_classes.push_back(std::move(coroutine));
    symbols.add(std::move(script));

    const auto* owner = symbols.find_path("strategies.gd");
    REQUIRE(owner != nullptr);
    const auto* base_symbol = symbols.find_inner(*owner, "Base");
    const auto* compatible_symbol = symbols.find_inner(*owner, "Compatible");
    const auto* coroutine_symbol = symbols.find_inner(*owner, "Coroutine");
    REQUIRE(base_symbol != nullptr);
    REQUIRE(compatible_symbol != nullptr);
    REQUIRE(coroutine_symbol != nullptr);
    REQUIRE(symbols.inner_base_of(*compatible_symbol) == base_symbol);
    REQUIRE(symbols.inner_base_of(*coroutine_symbol) == compatible_symbol);
    REQUIRE(symbols.find_inner_member(*coroutine_symbol, "transform") != nullptr);
    REQUIRE(symbols.requires_dynamic_dispatch(*base_symbol, "transform"));
    REQUIRE(symbols.may_dispatch_coroutine(*base_symbol, "transform"));
}

} // namespace
