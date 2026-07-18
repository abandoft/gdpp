#include "gdpp/codegen/cpp_generator.hpp"
#include "gdpp/semantic/godot_api.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace gdpp {
namespace {

std::string indent(std::size_t level) { return std::string(level * 4, ' '); }

std::string indent_block(const std::size_t level, std::string_view block) {
    if (block.empty())
        return {};
    const auto prefix = indent(level);
    std::string result;
    result.reserve(block.size() + prefix.size() * 4U);
    result += prefix;
    for (std::size_t index = 0; index < block.size(); ++index) {
        result.push_back(block[index]);
        if (block[index] == '\n' && index + 1U < block.size())
            result += prefix;
    }
    if (result.back() != '\n')
        result.push_back('\n');
    return result;
}

bool contains_preload(const ir::Expression& expression) {
    if (expression.kind == ir::ExpressionKind::call && !expression.operands.empty()) {
        const auto& callee = *expression.operands.front();
        if (callee.resolution == ir::ResolutionKind::intrinsic &&
            callee.intrinsic == IntrinsicKind::preload) {
            return true;
        }
    }
    return std::any_of(expression.operands.begin(), expression.operands.end(),
                       [](const auto& operand) { return contains_preload(*operand); });
}

bool cached_preload_field(const ir::Field& field) {
    return !field.is_constant && !field.onready && field.initializer &&
           contains_preload(*field.initializer);
}

bool managed_static_constant(const Type& type) {
    switch (type.kind) {
    case TypeKind::unknown:
    case TypeKind::variant:
    case TypeKind::string:
    case TypeKind::string_name:
    case TypeKind::array:
    case TypeKind::dictionary:
    case TypeKind::builtin:
    case TypeKind::object:
        return true;
    case TypeKind::nil:
    case TypeKind::boolean:
    case TypeKind::integer:
    case TypeKind::floating:
    case TypeKind::enumeration:
    case TypeKind::script_resource:
    case TypeKind::void_type:
        return false;
    }
    return true;
}

bool editor_safe_initializer(const ir::Expression& expression) {
    const auto all_operands_are_safe = [&expression](std::size_t begin = 0) {
        return std::all_of(expression.operands.begin() + static_cast<std::ptrdiff_t>(begin),
                           expression.operands.end(),
                           [](const auto& operand) { return editor_safe_initializer(*operand); });
    };

    switch (expression.kind) {
    case ir::ExpressionKind::literal:
        return true;
    case ir::ExpressionKind::identifier:
        return expression.resolution == ir::ResolutionKind::godot_type ||
               expression.resolution == ir::ResolutionKind::global_constant ||
               expression.resolution == ir::ResolutionKind::global_enum_type ||
               expression.resolution == ir::ResolutionKind::global_enum_value ||
               expression.resolution == ir::ResolutionKind::builtin_constant ||
               expression.resolution == ir::ResolutionKind::enum_member;
    case ir::ExpressionKind::unary:
    case ir::ExpressionKind::await_expression:
    case ir::ExpressionKind::binary:
    case ir::ExpressionKind::subscript:
    case ir::ExpressionKind::conditional:
    case ir::ExpressionKind::array_literal:
    case ir::ExpressionKind::dictionary_literal:
        return all_operands_are_safe();
    case ir::ExpressionKind::member:
        return (expression.resolution == ir::ResolutionKind::global_constant ||
                expression.resolution == ir::ResolutionKind::global_enum_type ||
                expression.resolution == ir::ResolutionKind::global_enum_value ||
                expression.resolution == ir::ResolutionKind::builtin_constant ||
                expression.resolution == ir::ResolutionKind::enum_member) &&
               all_operands_are_safe();
    case ir::ExpressionKind::call:
        // Value-type constructors such as Vector2(...) and Color(...) are pure.
        // Object constructors, utility functions, methods, preload/load, and
        // customer callables may touch the SceneTree or external services.
        return !expression.operands.empty() &&
               expression.operands.front()->resolution == ir::ResolutionKind::godot_constructor &&
               expression.type.kind != TypeKind::object && all_operands_are_safe(1);
    case ir::ExpressionKind::node_reference:
    case ir::ExpressionKind::lambda:
        return false;
    }
    return false;
}

const ir::Expression* range_call(const ir::Expression& expression) {
    if (expression.kind != ir::ExpressionKind::call || expression.operands.size() < 2 ||
        expression.operands.size() > 4) {
        return nullptr;
    }
    const auto& callee = *expression.operands.front();
    return callee.resolution == ir::ResolutionKind::intrinsic &&
                   callee.intrinsic == IntrinsicKind::range
               ? &expression
               : nullptr;
}

bool requires_native_fallback(const std::vector<ir::Statement>& statements) {
    // Match currently lowers to guarded branches plus a matched sentinel. Even an
    // exhaustive catch-all is therefore not provably exhaustive to a C++ compiler.
    // Semantic analysis has already guaranteed that this fallback is unreachable.
    return !statements.empty() && statements.back().kind == ir::StatementKind::match_statement;
}

bool contains_current_loop_break(const std::vector<ir::Statement>& statements) {
    for (const auto& statement : statements) {
        if (statement.kind == ir::StatementKind::break_statement)
            return true;
        if (statement.kind == ir::StatementKind::while_statement ||
            statement.kind == ir::StatementKind::for_statement)
            continue;
        if (contains_current_loop_break(statement.body) ||
            contains_current_loop_break(statement.else_body))
            return true;
    }
    return false;
}

bool contains_current_loop_control(const ir::Statement& statement) {
    if (statement.kind == ir::StatementKind::break_statement ||
        statement.kind == ir::StatementKind::continue_statement) {
        return true;
    }
    if (statement.kind == ir::StatementKind::while_statement ||
        statement.kind == ir::StatementKind::for_statement) {
        return false;
    }
    return std::any_of(statement.body.begin(), statement.body.end(),
                       contains_current_loop_control) ||
           std::any_of(statement.else_body.begin(), statement.else_body.end(),
                       contains_current_loop_control) ||
           std::any_of(statement.guard_prefix.begin(), statement.guard_prefix.end(),
                       contains_current_loop_control) ||
           std::any_of(statement.assert_condition_prefix.begin(),
                       statement.assert_condition_prefix.end(), contains_current_loop_control) ||
           std::any_of(statement.assert_message_prefix.begin(),
                       statement.assert_message_prefix.end(), contains_current_loop_control);
}

void collect_local_declarations(const std::vector<ir::Statement>& statements,
                                std::unordered_map<std::string, Type>& types,
                                std::unordered_set<std::string>& ambiguous) {
    for (const auto& statement : statements) {
        if (statement.kind == ir::StatementKind::variable ||
            statement.kind == ir::StatementKind::await_variable ||
            statement.kind == ir::StatementKind::for_statement) {
            if (const auto [found, inserted] =
                    types.emplace(statement.name, statement.declared_type);
                !inserted && found->second != statement.declared_type) {
                ambiguous.insert(statement.name);
            } else if (!inserted) {
                ambiguous.insert(statement.name);
            }
        }
        collect_local_declarations(statement.body, types, ambiguous);
        collect_local_declarations(statement.else_body, types, ambiguous);
        collect_local_declarations(statement.guard_prefix, types, ambiguous);
        collect_local_declarations(statement.assert_condition_prefix, types, ambiguous);
        collect_local_declarations(statement.assert_message_prefix, types, ambiguous);
    }
}

void collect_declared_names(const std::vector<ir::Statement>& statements,
                            std::unordered_set<std::string>& names) {
    for (const auto& statement : statements) {
        if (statement.kind == ir::StatementKind::variable ||
            statement.kind == ir::StatementKind::await_variable ||
            statement.kind == ir::StatementKind::for_statement) {
            names.insert(statement.name);
        }
        collect_declared_names(statement.body, names);
        collect_declared_names(statement.else_body, names);
        collect_declared_names(statement.guard_prefix, names);
        collect_declared_names(statement.assert_condition_prefix, names);
        collect_declared_names(statement.assert_message_prefix, names);
    }
}

void collect_assigned_names(const std::vector<ir::Statement>& statements,
                            std::unordered_set<std::string>& names) {
    for (const auto& statement : statements) {
        if (statement.kind == ir::StatementKind::assignment && statement.condition &&
            statement.condition->kind == ir::ExpressionKind::identifier &&
            statement.condition->resolution == ir::ResolutionKind::none) {
            names.insert(statement.condition->value);
        }
        collect_assigned_names(statement.body, names);
        collect_assigned_names(statement.else_body, names);
        collect_assigned_names(statement.guard_prefix, names);
        collect_assigned_names(statement.assert_condition_prefix, names);
        collect_assigned_names(statement.assert_message_prefix, names);
    }
}

bool native_statements_fall_through(const std::vector<ir::Statement>& statements);

bool flat_async_statement_supported(const ir::Statement& statement) {
    switch (statement.kind) {
    case ir::StatementKind::expression:
    case ir::StatementKind::assignment:
    case ir::StatementKind::await_statement:
    case ir::StatementKind::pass_statement:
        break;
    case ir::StatementKind::assert_statement:
        return statement.assert_condition_prefix.empty() && statement.assert_message_prefix.empty();
    case ir::StatementKind::if_statement:
        return std::all_of(statement.body.begin(), statement.body.end(),
                           flat_async_statement_supported) &&
               std::all_of(statement.else_body.begin(), statement.else_body.end(),
                           flat_async_statement_supported);
    case ir::StatementKind::return_statement:
    case ir::StatementKind::await_variable:
    case ir::StatementKind::variable:
    case ir::StatementKind::for_statement:
    case ir::StatementKind::while_statement:
    case ir::StatementKind::match_statement:
    case ir::StatementKind::match_branch:
    case ir::StatementKind::break_statement:
    case ir::StatementKind::continue_statement:
        return false;
    }
    return statement.body.empty() && statement.else_body.empty();
}

bool native_statement_falls_through(const ir::Statement& statement) {
    switch (statement.kind) {
    case ir::StatementKind::return_statement:
    case ir::StatementKind::break_statement:
    case ir::StatementKind::continue_statement:
        return false;
    case ir::StatementKind::if_statement:
        return statement.else_body.empty() || native_statements_fall_through(statement.body) ||
               native_statements_fall_through(statement.else_body);
    case ir::StatementKind::match_statement:
        // The sentinel-based native lowering is conservatively fall-through to C++.
        return true;
    case ir::StatementKind::while_statement:
        if (statement.condition && statement.condition->kind == ir::ExpressionKind::literal &&
            statement.condition->literal_kind == ir::LiteralKind::boolean &&
            statement.condition->value == "true" && !contains_current_loop_break(statement.body))
            return false;
        return true;
    default:
        return true;
    }
}

bool native_statements_fall_through(const std::vector<ir::Statement>& statements) {
    bool falls_through = true;
    for (const auto& statement : statements) {
        if (!falls_through)
            return false;
        falls_through = native_statement_falls_through(statement);
    }
    return falls_through;
}

std::string escaped_string(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\a':
            result += "\\a";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\v':
            result += "\\v";
            break;
        default:
            if (const auto byte = static_cast<unsigned char>(character);
                byte < 0x20U || byte == 0x7fU) {
                result.push_back('\\');
                result.push_back(static_cast<char>('0' + ((byte >> 6U) & 0x07U)));
                result.push_back(static_cast<char>('0' + ((byte >> 3U) & 0x07U)));
                result.push_back(static_cast<char>('0' + (byte & 0x07U)));
            } else {
                result.push_back(character);
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

bool is_ascii(std::string_view value) {
    return std::all_of(value.begin(), value.end(),
                       [](const char byte) { return static_cast<unsigned char>(byte) < 0x80; });
}

// godot-cpp's const char* constructors use Latin-1 for compatibility. Keep the
// compact form for ASCII, but explicitly decode UTF-8 whenever customer source
// contains non-ASCII text (node names, translations, methods, signals, etc.).
std::string godot_text_argument(const std::string& value) {
    if (is_ascii(value))
        return escaped_string(value);
    return "godot::String::utf8(" + escaped_string(value) + ")";
}

std::string godot_string(const std::string& value) {
    if (is_ascii(value))
        return "godot::String(" + escaped_string(value) + ")";
    return "godot::String::utf8(" + escaped_string(value) + ")";
}

std::string godot_string_name(const std::string& value) {
    return "godot::StringName(" + godot_text_argument(value) + ")";
}

std::string godot_node_path(const std::string& value) {
    return "godot::NodePath(" + godot_text_argument(value) + ")";
}

std::string builtin_constant_expression(std::string value) {
    std::size_t position = 0;
    while ((position = value.find("inf", position)) != std::string::npos) {
        value.replace(position, 3, "Math_INF");
        position += 8;
    }
    return "godot::" + value;
}

std::string to_snake_case(std::string_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (std::isupper(byte) != 0) {
            const bool previous_is_lower =
                index > 0 && std::islower(static_cast<unsigned char>(value[index - 1])) != 0;
            const bool next_starts_word =
                index > 0 && index + 1 < value.size() &&
                std::islower(static_cast<unsigned char>(value[index + 1])) != 0;
            if (!result.empty() && result.back() != '_' && (previous_is_lower || next_starts_word))
                result.push_back('_');
            result.push_back(static_cast<char>(std::tolower(byte)));
        } else if (std::isalnum(byte) != 0 || value[index] == '_') {
            result.push_back(value[index]);
        } else {
            result.push_back('_');
        }
    }
    return result.empty() ? "generated_script" : result;
}

std::string to_pascal_case(std::string_view value) {
    std::string result;
    bool uppercase = true;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) == 0) {
            uppercase = true;
            continue;
        }
        result.push_back(uppercase ? static_cast<char>(std::toupper(byte)) : character);
        uppercase = false;
    }
    return result.empty() ? "GeneratedScript" : result;
}

std::string header_for_base(const std::string& base) {
    return "godot_cpp/classes/" + to_snake_case(base) + ".hpp";
}

struct NativeTypeIncludes {
    std::set<std::string> builtins;
    std::set<std::string> objects;
    std::set<std::string> scripts;
    std::set<std::string> script_resources;
    std::set<std::string> complete_scripts;
    std::set<std::string> complete_script_resources;
    // Script display names are not identities: unnamed scripts can share a stem and project
    // globals can shadow engine types. Resolved native names remain unique across the project.
    std::set<std::string> resolved_native_scripts;
};

void collect_type(const Type& type, NativeTypeIncludes& includes, const GodotApi& api,
                  const ScriptSymbolTable* script_symbols) {
    if (type.kind == TypeKind::builtin)
        includes.builtins.insert(type.name);
    else if (type.kind == TypeKind::object && api.find_class(type.name))
        includes.objects.insert(type.name);
    else if (type.kind == TypeKind::object && script_symbols &&
             script_symbols->find_class(type.name))
        includes.scripts.insert(type.name);
    else if (type.kind == TypeKind::script_resource && script_symbols &&
             script_symbols->find_path(type.name))
        includes.script_resources.insert(type.name);
}

void collect_statement_types(const ir::Statement& statement, NativeTypeIncludes& includes,
                             const GodotApi& api, const ScriptSymbolTable* script_symbols);

void collect_header_expression_dependencies(const ir::Expression& expression,
                                            NativeTypeIncludes& includes) {
    if (expression.resolution == ir::ResolutionKind::script_type &&
        expression.type.kind == TypeKind::object) {
        includes.complete_scripts.insert(expression.type.name);
    }
    if (expression.resolution == ir::ResolutionKind::script_resource &&
        expression.type.kind == TypeKind::script_resource) {
        includes.complete_script_resources.insert(expression.type.name);
    }
    for (const auto& operand : expression.operands)
        collect_header_expression_dependencies(*operand, includes);
    if (expression.lambda) {
        for (const auto& parameter : expression.lambda->parameters) {
            if (parameter.default_value)
                collect_header_expression_dependencies(*parameter.default_value, includes);
        }
        for (const auto& statement : expression.lambda->body) {
            if (statement.expression)
                collect_header_expression_dependencies(*statement.expression, includes);
            if (statement.condition)
                collect_header_expression_dependencies(*statement.condition, includes);
        }
    }
}

void collect_expression_types(const ir::Expression& expression, NativeTypeIncludes& includes,
                              const GodotApi& api, const ScriptSymbolTable* script_symbols) {
    collect_type(expression.type, includes, api, script_symbols);
    if ((expression.resolution == ir::ResolutionKind::script_autoload ||
         expression.resolution == ir::ResolutionKind::script_type) &&
        !expression.resolved_owner.empty()) {
        includes.resolved_native_scripts.insert(expression.resolved_owner);
    }
    if (expression.kind == ir::ExpressionKind::call && !expression.operands.empty()) {
        const auto& callee = *expression.operands.front();
        if (callee.resolution == ir::ResolutionKind::godot_method) {
            if (const auto* method = api.find_method(callee.resolved_owner, callee.value)) {
                const auto provided = expression.operands.size() - 1;
                const auto count =
                    std::min(provided, static_cast<std::size_t>(method->maximum_arguments));
                for (std::size_t index = 0; index < count; ++index) {
                    if (const auto* argument = api.argument(*method, index))
                        collect_type(type_from_godot_api(argument->type), includes, api,
                                     script_symbols);
                }
            }
        }
    }
    for (const auto& operand : expression.operands)
        collect_expression_types(*operand, includes, api, script_symbols);
    if (expression.lambda) {
        collect_type(expression.lambda->return_type, includes, api, script_symbols);
        for (const auto& parameter : expression.lambda->parameters)
            collect_type(parameter.type, includes, api, script_symbols);
        for (const auto& statement : expression.lambda->body)
            collect_statement_types(statement, includes, api, script_symbols);
    }
}

void collect_statement_types(const ir::Statement& statement, NativeTypeIncludes& includes,
                             const GodotApi& api, const ScriptSymbolTable* script_symbols) {
    collect_type(statement.declared_type, includes, api, script_symbols);
    if (statement.expression)
        collect_expression_types(*statement.expression, includes, api, script_symbols);
    if (statement.condition)
        collect_expression_types(*statement.condition, includes, api, script_symbols);
    for (const auto& child : statement.body)
        collect_statement_types(child, includes, api, script_symbols);
    for (const auto& child : statement.else_body)
        collect_statement_types(child, includes, api, script_symbols);
    for (const auto& child : statement.guard_prefix)
        collect_statement_types(child, includes, api, script_symbols);
    for (const auto& child : statement.assert_condition_prefix)
        collect_statement_types(child, includes, api, script_symbols);
    for (const auto& child : statement.assert_message_prefix)
        collect_statement_types(child, includes, api, script_symbols);
}

void collect_class_types(const ir::Class& declaration, NativeTypeIncludes& includes,
                         const GodotApi& api, const ScriptSymbolTable* script_symbols) {
    collect_type({TypeKind::object, declaration.base_type}, includes, api, script_symbols);
    for (const auto& field : declaration.fields) {
        collect_type(field.type, includes, api, script_symbols);
        if (field.initializer)
            collect_expression_types(*field.initializer, includes, api, script_symbols);
        if (field.getter) {
            for (const auto& statement : field.getter->body)
                collect_statement_types(statement, includes, api, script_symbols);
        }
        if (field.setter) {
            for (const auto& statement : field.setter->body)
                collect_statement_types(statement, includes, api, script_symbols);
        }
    }
    for (const auto& function : declaration.functions) {
        collect_type(function.return_type, includes, api, script_symbols);
        for (const auto& parameter : function.parameters)
            collect_type(parameter.type, includes, api, script_symbols);
        for (const auto& parameter : function.parameters) {
            if (parameter.default_value)
                collect_header_expression_dependencies(*parameter.default_value, includes);
        }
        for (const auto& statement : function.body)
            collect_statement_types(statement, includes, api, script_symbols);
    }
    for (const auto& inner : declaration.classes)
        collect_class_types(inner, includes, api, script_symbols);
}

NativeTypeIncludes collect_native_types(const ir::Module& module, const GodotApi& api,
                                        const ScriptSymbolTable* script_symbols) {
    NativeTypeIncludes includes;
    for (const auto& field : module.fields) {
        collect_type(field.type, includes, api, script_symbols);
        if (field.initializer)
            collect_expression_types(*field.initializer, includes, api, script_symbols);
        if (field.getter) {
            for (const auto& statement : field.getter->body)
                collect_statement_types(statement, includes, api, script_symbols);
        }
        if (field.setter) {
            for (const auto& statement : field.setter->body)
                collect_statement_types(statement, includes, api, script_symbols);
        }
    }
    for (const auto& signal : module.signals) {
        for (const auto& parameter : signal.parameters)
            collect_type(parameter.type, includes, api, script_symbols);
    }
    for (const auto& function : module.functions) {
        collect_type(function.return_type, includes, api, script_symbols);
        for (const auto& parameter : function.parameters)
            collect_type(parameter.type, includes, api, script_symbols);
        for (const auto& parameter : function.parameters) {
            if (parameter.default_value)
                collect_header_expression_dependencies(*parameter.default_value, includes);
        }
        for (const auto& statement : function.body)
            collect_statement_types(statement, includes, api, script_symbols);
    }
    for (const auto& declaration : module.classes)
        collect_class_types(declaration, includes, api, script_symbols);
    return includes;
}

std::string variant_type(const Type& type) {
    switch (type.kind) {
    case TypeKind::boolean:
        return "godot::Variant::BOOL";
    case TypeKind::integer:
        return "godot::Variant::INT";
    case TypeKind::floating:
        return "godot::Variant::FLOAT";
    case TypeKind::string:
        return "godot::Variant::STRING";
    case TypeKind::string_name:
        return "godot::Variant::STRING_NAME";
    case TypeKind::array:
        return "godot::Variant::ARRAY";
    case TypeKind::dictionary:
        return "godot::Variant::DICTIONARY";
    case TypeKind::enumeration:
        return "godot::Variant::INT";
    case TypeKind::script_resource:
        return "godot::Variant::NIL";
    case TypeKind::builtin: {
        static const std::unordered_map<std::string, std::string> builtin_types{
            {"Vector2", "VECTOR2"},
            {"Vector2i", "VECTOR2I"},
            {"Rect2", "RECT2"},
            {"Rect2i", "RECT2I"},
            {"Vector3", "VECTOR3"},
            {"Vector3i", "VECTOR3I"},
            {"Transform2D", "TRANSFORM2D"},
            {"Vector4", "VECTOR4"},
            {"Vector4i", "VECTOR4I"},
            {"Plane", "PLANE"},
            {"Quaternion", "QUATERNION"},
            {"AABB", "AABB"},
            {"Basis", "BASIS"},
            {"Transform3D", "TRANSFORM3D"},
            {"Projection", "PROJECTION"},
            {"Color", "COLOR"},
            {"NodePath", "NODE_PATH"},
            {"RID", "RID"},
            {"Callable", "CALLABLE"},
            {"Signal", "SIGNAL"},
            {"PackedByteArray", "PACKED_BYTE_ARRAY"},
            {"PackedInt32Array", "PACKED_INT32_ARRAY"},
            {"PackedInt64Array", "PACKED_INT64_ARRAY"},
            {"PackedFloat32Array", "PACKED_FLOAT32_ARRAY"},
            {"PackedFloat64Array", "PACKED_FLOAT64_ARRAY"},
            {"PackedStringArray", "PACKED_STRING_ARRAY"},
            {"PackedVector2Array", "PACKED_VECTOR2_ARRAY"},
            {"PackedVector3Array", "PACKED_VECTOR3_ARRAY"},
            {"PackedColorArray", "PACKED_COLOR_ARRAY"},
            {"PackedVector4Array", "PACKED_VECTOR4_ARRAY"},
        };
        if (const auto found = builtin_types.find(type.name); found != builtin_types.end())
            return "godot::Variant::" + found->second;
        return "godot::Variant::NIL";
    }
    case TypeKind::object:
        return "godot::Variant::OBJECT";
    default:
        return "godot::Variant::NIL";
    }
}

std::string property_hint(const ir::Field& field, const GodotApi& api,
                          const ScriptSymbolTable* script_symbols) {
    if (!field.property)
        return {};
    if (field.property_type.kind == TypeKind::enumeration) {
        if (script_symbols) {
            const auto separator = field.property_type.name.find('.');
            if (separator != std::string::npos) {
                if (const auto* owner = script_symbols->find_external(
                        field.property_type.name.substr(0, separator))) {
                    if (const auto* enumeration = script_symbols->find_external_enum(
                            *owner, field.property_type.name.substr(separator + 1));
                        enumeration && enumeration->is_bitfield) {
                        return "godot::PROPERTY_HINT_FLAGS";
                    }
                }
            }
        }
        return "godot::PROPERTY_HINT_ENUM";
    }
    // Godot 4.4 predates PROPERTY_HINT_FILE_PATH. The latest-language annotation is still
    // accepted and lowered to its closest 4.4 inspector behavior instead of emitting a C++ enum
    // constant that does not exist in the selected SDK.
    if (field.property->name == "export_file_path" && api.version().rfind("4.4", 0) == 0)
        return "godot::PROPERTY_HINT_FILE";
    static const std::unordered_map<std::string, std::string> hints{
        {"export_range", "godot::PROPERTY_HINT_RANGE"},
        {"export_enum", "godot::PROPERTY_HINT_ENUM"},
        {"export_flags", "godot::PROPERTY_HINT_FLAGS"},
        {"export_file", "godot::PROPERTY_HINT_FILE"},
        {"export_file_path", "godot::PROPERTY_HINT_FILE_PATH"},
        {"export_global_file", "godot::PROPERTY_HINT_GLOBAL_FILE"},
        {"export_dir", "godot::PROPERTY_HINT_DIR"},
        {"export_global_dir", "godot::PROPERTY_HINT_GLOBAL_DIR"},
        {"export_multiline", "godot::PROPERTY_HINT_MULTILINE_TEXT"},
        {"export_color_no_alpha", "godot::PROPERTY_HINT_COLOR_NO_ALPHA"},
        {"export_node_path", "godot::PROPERTY_HINT_NODE_PATH_VALID_TYPES"},
        {"export_placeholder", "godot::PROPERTY_HINT_PLACEHOLDER_TEXT"},
        {"export_exp_easing", "godot::PROPERTY_HINT_EXP_EASING"},
        {"export_tool_button", "godot::PROPERTY_HINT_TOOL_BUTTON"},
        {"export_flags_2d_render", "godot::PROPERTY_HINT_LAYERS_2D_RENDER"},
        {"export_flags_2d_physics", "godot::PROPERTY_HINT_LAYERS_2D_PHYSICS"},
        {"export_flags_2d_navigation", "godot::PROPERTY_HINT_LAYERS_2D_NAVIGATION"},
        {"export_flags_3d_render", "godot::PROPERTY_HINT_LAYERS_3D_RENDER"},
        {"export_flags_3d_physics", "godot::PROPERTY_HINT_LAYERS_3D_PHYSICS"},
        {"export_flags_3d_navigation", "godot::PROPERTY_HINT_LAYERS_3D_NAVIGATION"},
        {"export_flags_avoidance", "godot::PROPERTY_HINT_LAYERS_AVOIDANCE"},
    };
    if (field.property->name == "export_custom" && !field.property->arguments.empty()) {
        auto hint = field.property->arguments.front().value;
        if (hint.rfind("PROPERTY_HINT_", 0) == 0)
            hint = "godot::" + hint;
        return "static_cast<godot::PropertyHint>(" + hint + ")";
    }
    if (const auto found = hints.find(field.property->name); found != hints.end())
        return found->second;
    if (field.property_type.kind == TypeKind::object &&
        api.inherits(field.property_type.name, "Resource")) {
        return "godot::PROPERTY_HINT_RESOURCE_TYPE";
    }
    if (field.property_type.kind == TypeKind::object &&
        api.inherits(field.property_type.name, "Node")) {
        return "godot::PROPERTY_HINT_NODE_TYPE";
    }
    return {};
}

std::string property_hint_string(const ir::Field& field, const ScriptSymbolTable* script_symbols) {
    if (!field.property)
        return {};
    if (field.property_type.kind == TypeKind::enumeration) {
        if (!field.enum_hint.empty())
            return field.enum_hint;
        if (script_symbols) {
            const auto separator = field.property_type.name.find('.');
            if (separator != std::string::npos) {
                if (const auto* owner = script_symbols->find_global(
                        field.property_type.name.substr(0, separator))) {
                    if (const auto* enumeration = script_symbols->find_enum(
                            *owner, field.property_type.name.substr(separator + 1))) {
                        std::string result;
                        for (const auto& entry : enumeration->entries) {
                            if (!result.empty())
                                result.push_back(',');
                            result += entry.name + ":" + std::to_string(entry.value);
                        }
                        return result;
                    }
                }
                if (const auto* owner = script_symbols->find_external(
                        field.property_type.name.substr(0, separator))) {
                    if (const auto* enumeration = script_symbols->find_external_enum(
                            *owner, field.property_type.name.substr(separator + 1))) {
                        std::string result;
                        for (const auto& entry : enumeration->entries) {
                            if (!result.empty())
                                result.push_back(',');
                            result += entry.name + ":" + std::to_string(entry.value);
                        }
                        return result;
                    }
                }
            }
        }
        return {};
    }
    if (field.property->name == "export" && field.property_type.kind == TypeKind::object)
        return field.property_type.name;
    std::size_t argument_begin = 0;
    std::size_t argument_end = field.property->arguments.size();
    if (field.property->name == "export_custom") {
        argument_begin = std::min<std::size_t>(1, argument_end);
        argument_end = std::min<std::size_t>(2, argument_end);
    }
    std::string result;
    for (std::size_t index = argument_begin; index < argument_end; ++index) {
        const auto& argument = field.property->arguments[index];
        if (!result.empty())
            result.push_back(',');
        auto value = argument.value;
        if (argument.kind == ir::PropertyArgumentKind::number) {
            value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
        }
        result += value;
    }
    return result;
}

bool is_bound_property(const ir::Field& field) {
    // GDScript instance variables are always visible through Object.get/set,
    // even without @export or custom accessors. Generated native classes must
    // expose the same surface or cross-script expressions such as
    // `other.open_bag` silently lose their semantics after AOT conversion.
    // property_info() marks plain variables as SCRIPT_VARIABLE-only, so they
    // stay dynamically accessible without becoming editor/storage properties.
    return !field.is_static && !field.is_constant;
}

std::string property_info(const ir::Field& field, const GodotApi& api,
                          const ScriptSymbolTable* script_symbols) {
    std::string result = "godot::PropertyInfo(" + variant_type(field.property_type) + ", " +
                         godot_text_argument(field.name);
    const auto hint = property_hint(field, api, script_symbols);
    const auto hint_string = property_hint_string(field, script_symbols);
    std::string usage;
    if (field.property && field.property->name == "export_storage") {
        usage = "godot::PROPERTY_USAGE_STORAGE";
    } else if (field.property && field.property->name == "export_tool_button") {
        usage = "godot::PROPERTY_USAGE_EDITOR";
    } else if (field.property && field.property->name == "export_custom" &&
               field.property->arguments.size() >= 3) {
        usage = field.property->arguments[2].value;
        if (usage.rfind("PROPERTY_USAGE_", 0) == 0)
            usage = "godot::" + usage;
        usage = "static_cast<uint32_t>(" + usage + ")";
    } else if (!field.property) {
        usage = "godot::PROPERTY_USAGE_SCRIPT_VARIABLE";
    }
    // PropertyInfo's hint and usage occupy one fixed argument tuple. Building them in separate
    // annotation branches used to duplicate the hint pair for combinations such as
    // `@export_storage` plus an enum/resource type.
    if (!hint.empty() || !hint_string.empty() || !usage.empty()) {
        result += ", " + (hint.empty() ? "godot::PROPERTY_HINT_NONE" : hint) + ", " +
                  godot_string(hint_string);
        if (!usage.empty())
            result += ", " + usage;
    }
    return result + ")";
}

std::string variant_operator(const std::string& operation) {
    static const std::unordered_map<std::string, std::string> operators{
        {"==", "OP_EQUAL"},       {"!=", "OP_NOT_EQUAL"}, {"<", "OP_LESS"},
        {"<=", "OP_LESS_EQUAL"},  {">", "OP_GREATER"},    {">=", "OP_GREATER_EQUAL"},
        {"+", "OP_ADD"},          {"-", "OP_SUBTRACT"},   {"*", "OP_MULTIPLY"},
        {"/", "OP_DIVIDE"},       {"%", "OP_MODULE"},     {"<<", "OP_SHIFT_LEFT"},
        {">>", "OP_SHIFT_RIGHT"}, {"&", "OP_BIT_AND"},    {"|", "OP_BIT_OR"},
        {"^", "OP_BIT_XOR"},      {"**", "OP_POWER"},     {"and", "OP_AND"},
        {"or", "OP_OR"},          {"in", "OP_IN"},        {"not", "OP_NOT"},
        {"~", "OP_BIT_NEGATE"},
    };
    const auto found = operators.find(operation);
    return found == operators.end() ? std::string{} : found->second;
}

} // namespace

std::string CodeGenerator::sanitize_identifier(const std::string& value) {
    static const std::unordered_set<std::string_view> reserved{
        "alignas",      "alignof",  "and",       "and_eq",   "asm",       "auto",
        "bitand",       "bitor",    "bool",      "break",    "case",      "catch",
        "char",         "class",    "compl",     "const",    "constexpr", "const_cast",
        "continue",     "decltype", "default",   "delete",   "do",        "double",
        "dynamic_cast", "else",     "enum",      "explicit", "export",    "extern",
        "false",        "float",    "for",       "friend",   "goto",      "if",
        "inline",       "int",      "long",      "mutable",  "namespace", "new",
        "noexcept",     "not",      "not_eq",    "nullptr",  "operator",  "or",
        "or_eq",        "private",  "protected", "public",   "register",  "reinterpret_cast",
        "return",       "short",    "signed",    "sizeof",   "static",    "static_assert",
        "static_cast",  "struct",   "switch",    "template", "this",      "thread_local",
        "throw",        "true",     "try",       "typedef",  "typeid",    "typename",
        "typeof",       "union",    "unsigned",  "using",    "virtual",   "void",
        "volatile",     "wchar_t",  "while",     "xor",      "xor_eq",
    };

    constexpr std::string_view encoded_prefix{"_gdpp_id_"};
    constexpr std::string_view internal_prefix{"_gdpp_"};
    const auto is_plain_identifier =
        !value.empty() &&
        (std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_') &&
        std::all_of(value.begin() + 1, value.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return std::isalnum(byte) != 0 || character == '_';
        });
    const auto is_implementation_reserved =
        value.size() >= 2 && value.front() == '_' &&
        (value[1] == '_' || std::isupper(static_cast<unsigned char>(value[1])) != 0);
    if (is_plain_identifier && reserved.find(value) == reserved.end() &&
        value.rfind(internal_prefix, 0) != 0 && !is_implementation_reserved) {
        return value;
    }

    // Native identifiers are deliberately ASCII and injective. Encoding the complete UTF-8 byte
    // sequence avoids normalization/toolchain differences and prevents collisions such as
    // `template` versus a user-authored `_gdpp_id_74656d706c617465`.
    constexpr char hexadecimal[] = "0123456789abcdef";
    std::string encoded{encoded_prefix};
    encoded.reserve(encoded_prefix.size() + value.size() * 2U);
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        encoded.push_back(hexadecimal[byte >> 4U]);
        encoded.push_back(hexadecimal[byte & 0x0fU]);
    }
    return encoded;
}

std::string CodeGenerator::enum_identifier(const std::string& value) {
    // System and third-party headers expose many object-like macros (SING is one common
    // example from <math.h>). Enum storage names are internal implementation details, so use a
    // collision-proof native prefix while ClassDB continues to publish the original GDS name.
    return "_gdpp_enum_" + sanitize_identifier(value);
}

std::string CodeGenerator::sanitize_qualified_identifier(std::string_view value) {
    std::string result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto separator = value.find("::", begin);
        const auto end = separator == std::string_view::npos ? value.size() : separator;
        if (!result.empty())
            result += "::";
        result += sanitize_identifier(std::string{value.substr(begin, end - begin)});
        if (separator == std::string_view::npos)
            break;
        begin = separator + 2;
    }
    return result;
}

bool is_valid_base_type(const std::string& value) {
    if (value.empty() ||
        (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_')) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_';
    });
}

std::string CodeGenerator::cpp_type(const Type& type) const {
    switch (type.kind) {
    case TypeKind::void_type:
        return "void";
    case TypeKind::boolean:
        return "bool";
    case TypeKind::integer:
        return "int64_t";
    case TypeKind::floating:
        return "double";
    case TypeKind::string:
        return "godot::String";
    case TypeKind::string_name:
        return "godot::StringName";
    case TypeKind::array:
        return "godot::Array";
    case TypeKind::dictionary:
        return "godot::Dictionary";
    case TypeKind::enumeration:
        return "int64_t";
    case TypeKind::script_resource:
        if (script_symbols_) {
            if (const auto* symbol = script_symbols_->find_path(type.name))
                return detail_namespace_ + "::ScriptResource<" + symbol->native_class_name + ">";
        }
        return "godot::Variant";
    case TypeKind::builtin:
        return "godot::" + type.name;
    case TypeKind::object:
        if (!api_.find_class(type.name)) {
            if (const auto inner = inner_cpp_type(type.name); !inner.empty())
                return "godot::Ref<" + inner + ">";
            if (script_symbols_) {
                if (const auto* symbol = script_symbols_->find_class(type.name)) {
                    if (api_.inherits(symbol->godot_base_type, "RefCounted"))
                        return "godot::Ref<" + symbol->native_class_name + ">";
                    return symbol->native_class_name + "*";
                }
            }
            return "godot::Variant";
        }
        if (api_.inherits(type.name, "RefCounted"))
            return "godot::Ref<godot::" + type.name + ">";
        return "godot::" + type.name + "*";
    default:
        return "godot::Variant";
    }
}

std::string CodeGenerator::inner_cpp_type(std::string_view name) const {
    if (const auto exact = inner_native_names_.find(std::string{name});
        exact != inner_native_names_.end()) {
        return exact->second;
    }
    const auto separator = name.rfind('.');
    if (separator != std::string_view::npos)
        name.remove_prefix(separator + 1);
    const std::string* unique = nullptr;
    for (const auto& [qualified, native] : inner_native_names_) {
        const auto leaf_separator = qualified.rfind('.');
        const auto leaf = leaf_separator == std::string::npos
                              ? std::string_view{qualified}
                              : std::string_view{qualified}.substr(leaf_separator + 1);
        if (leaf != name)
            continue;
        if (unique)
            return {};
        unique = &native;
    }
    return unique ? *unique : std::string{};
}

std::string CodeGenerator::inner_godot_base_type(std::string_view name) const {
    if (const auto exact = inner_godot_base_types_.find(std::string{name});
        exact != inner_godot_base_types_.end()) {
        return exact->second;
    }
    const auto separator = name.rfind('.');
    if (separator != std::string_view::npos)
        name.remove_prefix(separator + 1);
    const auto found = inner_godot_base_types_.find(std::string{name});
    return found == inner_godot_base_types_.end() ? std::string{name} : found->second;
}

std::string CodeGenerator::native_super_owner(std::string_view owner) const {
    const auto inner = inner_cpp_type(owner);
    return inner.empty() ? std::string{owner} : inner;
}

std::string CodeGenerator::script_method_native_name(const ScriptClassSymbol& owner,
                                                     const ScriptMemberSymbol& method) const {
    const auto source_name = sanitize_identifier(method.name);
    if (!script_symbols_ || method.is_static || method.name == "_init")
        return source_name;
    const auto inherited = script_symbols_->inherited_members(owner);
    const auto base = std::find_if(inherited.begin(), inherited.end(), [&](const auto* candidate) {
        return candidate->kind == ScriptMemberKind::function && !candidate->is_static &&
               candidate->name == method.name;
    });
    if (base == inherited.end())
        return source_name;
    const ScriptClassSymbol* base_owner = script_symbols_->base_of(owner);
    while (base_owner) {
        const auto declared = std::find_if(
            base_owner->members.begin(), base_owner->members.end(), [&](const auto& candidate) {
                return candidate.kind == ScriptMemberKind::function && !candidate.is_static &&
                       candidate.name == method.name;
            });
        if (declared != base_owner->members.end())
            break;
        base_owner = script_symbols_->base_of(*base_owner);
    }
    const auto coroutine_native_abi = [this](const ScriptClassSymbol& script,
                                             const ScriptMemberSymbol& candidate) {
        const auto* engine_method = api_.find_method(script.godot_base_type, candidate.name);
        return candidate.is_coroutine && !(engine_method && engine_method->is_virtual);
    };
    const auto same_native_abi = method.type == (*base)->type &&
                                 method.parameters == (*base)->parameters &&
                                 method.default_parameters == (*base)->default_parameters &&
                                 coroutine_native_abi(owner, method) ==
                                     coroutine_native_abi(base_owner ? *base_owner : owner, **base);
    // GDScript permits an override to change annotations and to become a coroutine. C++ cannot
    // overload solely by return type, so an ABI-incompatible override receives a private native
    // symbol while ClassDB continues to publish the original source method name.
    return same_native_abi ? source_name : "_gdpp_native_override_" + source_name;
}

bool CodeGenerator::inner_overrides_method(std::string_view base,
                                           std::string_view method) const noexcept {
    std::unordered_set<std::string> visited;
    std::string current{base};
    while (!current.empty() && visited.insert(current).second) {
        if (const auto methods = inner_method_names_.find(current);
            methods != inner_method_names_.end() &&
            methods->second.find(std::string{method}) != methods->second.end()) {
            return true;
        }
        const auto parent = inner_base_names_.find(current);
        current = parent == inner_base_names_.end() ? std::string{} : parent->second;
    }
    return false;
}

bool CodeGenerator::managed_constant_field(const ir::Field& field) const {
    if (!field.is_constant)
        return false;
    const auto uses_signature = [&](const auto& members) -> std::optional<bool> {
        const auto found = std::find_if(members.begin(), members.end(), [&](const auto& member) {
            return member.kind == ScriptMemberKind::constant && member.name == field.name;
        });
        if (found == members.end())
            return std::nullopt;
        return managed_static_constant(found->type);
    };
    if (current_inner_script_) {
        if (const auto result = uses_signature(current_inner_script_->members))
            return *result;
    } else if (current_script_) {
        if (const auto result = uses_signature(current_script_->members))
            return *result;
    }
    return managed_static_constant(field.type);
}

bool CodeGenerator::managed_constant_reference(const ir::Expression& expression) const {
    const auto uses_signature = [&](const auto& members) -> std::optional<bool> {
        const auto found = std::find_if(members.begin(), members.end(), [&](const auto& member) {
            return member.kind == ScriptMemberKind::constant && member.name == expression.value;
        });
        if (found == members.end())
            return std::nullopt;
        return managed_static_constant(found->type);
    };
    if (expression.resolved_owner.empty()) {
        if (current_inner_script_) {
            if (const auto result = uses_signature(current_inner_script_->members))
                return *result;
        } else if (current_script_) {
            if (const auto result = uses_signature(current_script_->members))
                return *result;
        }
    }
    return managed_static_constant(expression.type);
}

std::string CodeGenerator::emit_conversion(const Type& target, const Type& source,
                                           std::string value) const {
    if (target.is_dynamic())
        return value;
    const bool target_external = target.kind == TypeKind::object && script_symbols_ &&
                                 script_symbols_->find_external(target.name);
    const bool source_external = source.kind == TypeKind::object && script_symbols_ &&
                                 script_symbols_->find_external(source.name);
    if (target_external)
        return "godot::Variant(" + value + ")";
    if (source.kind == TypeKind::nil &&
        (target.kind == TypeKind::object || target.kind == TypeKind::array ||
         target.kind == TypeKind::dictionary || target.kind == TypeKind::string ||
         target.kind == TypeKind::string_name))
        return "{}";
    if (target.kind == TypeKind::object && source.kind == TypeKind::object &&
        target.name != source.name) {
        auto source_object = value;
        if (source_external)
            source_object = "(godot::Variant(" + source_object + ")).get_validated_object()";
        bool source_ref_counted =
            api_.inherits(source.name, "RefCounted") || !inner_cpp_type(source.name).empty();
        if (!source_ref_counted && script_symbols_) {
            const auto* symbol = script_symbols_->find_class(source.name);
            if (!symbol && current_script_ && current_script_->script_name == source.name)
                symbol = current_script_;
            source_ref_counted = symbol && api_.inherits(symbol->godot_base_type, "RefCounted");
        }
        if (source_ref_counted && !source_external)
            source_object = "(" + source_object + ").ptr()";
        const auto target_cpp = cpp_type(target);
        const std::string ref_prefix{"godot::Ref<"};
        if (target_cpp.rfind(ref_prefix, 0) == 0 && target_cpp.back() == '>') {
            const auto object_cpp =
                target_cpp.substr(ref_prefix.size(), target_cpp.size() - ref_prefix.size() - 1);
            return target_cpp + "(godot::Object::cast_to<" + object_cpp + ">(" + source_object +
                   "))";
        }
        auto object_cpp = target_cpp;
        if (!object_cpp.empty() && object_cpp.back() == '*')
            object_cpp.pop_back();
        return "godot::Object::cast_to<" + object_cpp + ">(" + source_object + ")";
    }
    if (!source.is_dynamic()) {
        if (target.kind == TypeKind::builtin && target.name == "Color" &&
            source.kind == TypeKind::integer) {
            return "godot::Color::hex(static_cast<uint32_t>(" + value + "))";
        }
        if (target.kind == TypeKind::builtin && target.name == "RID" &&
            source.kind == TypeKind::object && api_.find_method(source.name, "get_rid")) {
            if (api_.inherits(source.name, "RefCounted")) {
                return "((" + value + ").is_valid() ? (" + value + ")->get_rid() : godot::RID())";
            }
            return "((" + value + ") != nullptr ? (" + value + ")->get_rid() : godot::RID())";
        }
        if ((target.kind == TypeKind::integer || target.kind == TypeKind::floating ||
             target.kind == TypeKind::boolean) &&
            (source.kind == TypeKind::string || source.kind == TypeKind::string_name)) {
            return "static_cast<" + cpp_type(target) + ">(godot::Variant(" + value + "))";
        }
        if (target.kind == TypeKind::string && source.kind == TypeKind::string_name)
            return "godot::String(" + value + ")";
        if (target.kind == TypeKind::string_name && source.kind == TypeKind::string)
            return "godot::StringName(" + value + ")";
        if (target.kind == TypeKind::array && source.is_packed_array())
            return "godot::Array(" + value + ")";
        if (target.kind == TypeKind::integer && source.kind == TypeKind::boolean)
            return "static_cast<int64_t>(" + value + ")";
        if (target.is_numeric() && source.is_numeric() && target.kind != source.kind)
            return "static_cast<" + cpp_type(target) + ">(" + value + ")";
        if (target.kind == TypeKind::builtin && target != source) {
            for (std::size_t occurrence = 0;; ++occurrence) {
                const auto* constructor = api_.find_constructor(target.name, 1, occurrence);
                if (!constructor)
                    break;
                const auto* argument = api_.argument(*constructor, 0);
                if (argument && is_assignable(type_from_godot_api(argument->type), source)) {
                    return cpp_type(target) + "(" + value + ")";
                }
            }
        }
        return value;
    }
    if (target.kind == TypeKind::object) {
        const auto target_cpp = cpp_type(target);
        const std::string ref_prefix{"godot::Ref<"};
        if (target_cpp.rfind(ref_prefix, 0) == 0 && target_cpp.back() == '>') {
            const auto object_cpp =
                target_cpp.substr(ref_prefix.size(), target_cpp.size() - ref_prefix.size() - 1);
            return target_cpp + "(godot::Object::cast_to<" + object_cpp + ">((" + value +
                   ").get_validated_object()))";
        }
        auto object_cpp = target_cpp;
        if (!object_cpp.empty() && object_cpp.back() == '*')
            object_cpp.pop_back();
        return "godot::Object::cast_to<" + object_cpp + ">((" + value + ").get_validated_object())";
    }
    return "static_cast<" + cpp_type(target) + ">(" + value + ")";
}

std::string CodeGenerator::emit_parameter_default(const ir::Parameter& parameter) const {
    if (!parameter.default_value)
        return {};
    // C++ default arguments are evaluated in the caller and cannot reference the callee's
    // fields. Route every GDScript default through a private Variant marker so even typed,
    // instance-dependent expressions retain GDScript's per-invocation semantics.
    return "gdpp::runtime::default_argument()";
}

std::string CodeGenerator::parameter_native_type(const ir::Parameter& parameter) const {
    return parameter.default_value ? "godot::Variant" : cpp_type(parameter.type);
}

std::string CodeGenerator::parameter_native_name(const ir::Parameter& parameter) const {
    const auto name = sanitize_identifier(parameter.name);
    return parameter.default_value ? "_gdpp_argument_" + name : name;
}

std::string
CodeGenerator::emit_parameter_default_initializers(const std::vector<ir::Parameter>& parameters,
                                                   const std::size_t indent) const {
    std::string result;
    const std::string prefix(indent * 4, ' ');
    for (const auto& parameter : parameters) {
        if (!parameter.default_value)
            continue;
        const auto native_name = parameter_native_name(parameter);
        const auto source_name = sanitize_identifier(parameter.name);
        auto fallback = emit_conversion(parameter.type, parameter.default_value->type,
                                        emit_expression(*parameter.default_value));
        if (fallback == "{}") {
            const auto native_type = cpp_type(parameter.type);
            fallback =
                !native_type.empty() && native_type.back() == '*' ? "nullptr" : native_type + "{}";
        }
        if (parameter.type.is_dynamic())
            fallback = "godot::Variant(" + fallback + ")";
        const auto supplied =
            emit_conversion(parameter.type, {TypeKind::variant, "Variant"}, native_name);
        result += prefix + cpp_type(parameter.type) + " " + source_name + " = " +
                  "gdpp::runtime::is_default_argument(" + native_name + ") ? " + fallback + " : " +
                  supplied + ";\n";
    }
    return result;
}

std::string
CodeGenerator::emit_bound_parameter_defaults(const std::vector<ir::Parameter>& parameters) const {
    std::string result;
    for (const auto& parameter : parameters) {
        if (!parameter.default_value)
            continue;
        // The bound method receives the same marker as direct native calls and evaluates the
        // source expression inside the target instance.
        result += ", DEFVAL(gdpp::runtime::default_argument())";
    }
    return result;
}

std::string CodeGenerator::emit_api_argument(std::string_view api_type,
                                             std::string_view native_meta, const Type& source,
                                             std::string value) const {
    const auto comma = api_type.find(',');
    if (comma != std::string_view::npos)
        api_type = api_type.substr(0, comma);
    while (!api_type.empty() && api_type.front() == '-')
        api_type.remove_prefix(1);
    const bool bitfield = api_type.rfind("bitfield::", 0) == 0;
    if (bitfield || api_type.rfind("enum::", 0) == 0) {
        api_type.remove_prefix(bitfield ? std::string_view{"bitfield::"}.size()
                                        : std::string_view{"enum::"}.size());
        std::string cpp_name{api_type};
        std::replace(cpp_name.begin(), cpp_name.end(), '.', ':');
        for (std::size_t position = 0;
             (position = cpp_name.find(':', position)) != std::string::npos; position += 2) {
            if (position + 1 >= cpp_name.size() || cpp_name[position + 1] != ':')
                cpp_name.insert(position, 1, ':');
        }
        const auto integer = "static_cast<int64_t>(godot::Variant(" + value + "))";
        if (bitfield)
            return "godot::BitField<godot::" + cpp_name + ">(" + integer + ")";
        return "static_cast<godot::" + cpp_name + ">(" + integer + ")";
    }
    auto converted = emit_conversion(type_from_godot_api(api_type), source, std::move(value));
    std::string native_type;
    if (native_meta == "real_t")
        native_type = "godot::real_t";
    else if (native_meta == "float" || native_meta == "double")
        native_type = std::string{native_meta};
    else if (native_meta == "int8" || native_meta == "int16" || native_meta == "int32" ||
             native_meta == "int64" || native_meta == "uint8" || native_meta == "uint16" ||
             native_meta == "uint32" || native_meta == "uint64")
        native_type = std::string{native_meta} + "_t";
    if (!native_type.empty())
        return "static_cast<" + native_type + ">(" + converted + ")";
    return converted;
}

std::string CodeGenerator::emit_api_return(const Type& target, std::string value) const {
    // extension_api.json exposes integer and float as GDScript's fixed semantic types while the
    // generated godot-cpp method can use uint32_t, uint64_t, float or real_t. Make that ABI
    // boundary explicit so strict GCC/MSVC builds do not rely on an implicit narrowing or signed
    // conversion. Non-scalar Godot values already use the exact generated wrapper type.
    if (target.kind == TypeKind::integer || target.kind == TypeKind::enumeration)
        return "static_cast<int64_t>(" + value + ")";
    if (target.kind == TypeKind::floating)
        return "static_cast<double>(" + value + ")";
    if (target.kind == TypeKind::boolean)
        return "static_cast<bool>(" + value + ")";
    return value;
}

std::string CodeGenerator::emit_subscript_read(const Type& container, const Type& result,
                                               std::string value) const {
    if ((container.kind == TypeKind::array || container.kind == TypeKind::dictionary) &&
        !result.is_dynamic()) {
        return emit_conversion(result, {TypeKind::variant, "Variant"}, std::move(value));
    }
    if (container.kind == TypeKind::string) {
        return "godot::String::chr(static_cast<int64_t>(" + value + "))";
    }
    if (container.is_packed_array())
        return emit_api_return(result, std::move(value));
    return value;
}

std::string CodeGenerator::emit_subscript_store(const Type& container, std::string value) const {
    std::string native_type;
    if (container.name == "PackedByteArray")
        native_type = "uint8_t";
    else if (container.name == "PackedInt32Array")
        native_type = "int32_t";
    else if (container.name == "PackedInt64Array")
        native_type = "int64_t";
    else if (container.name == "PackedFloat32Array")
        native_type = "float";
    else if (container.name == "PackedFloat64Array")
        native_type = "double";
    if (!native_type.empty())
        return "static_cast<" + native_type + ">(" + value + ")";
    return value;
}

std::string CodeGenerator::emit_direct_builtin_member(std::string_view owner, std::string object,
                                                      std::string_view member) const {
    const auto component_index = [](std::string_view name) -> int {
        if (name == "x")
            return 0;
        if (name == "y")
            return 1;
        if (name == "z")
            return 2;
        return name == "w" ? 3 : -1;
    };
    const auto index = component_index(member);
    std::string result;
    if (owner == "Basis" && index >= 0)
        result = object + ".get_column(" + std::to_string(index) + ")";
    else if (owner == "Projection" && index >= 0)
        result = object + "[" + std::to_string(index) + "]";
    else if (owner == "Transform2D" && member == "origin")
        result = object + ".get_origin()";
    else if ((owner == "Rect2" || owner == "Rect2i" || owner == "AABB") && member == "end")
        result = object + ".get_end()";
    else
        result = object + "." + sanitize_identifier(std::string{member});
    if (const auto* property = api_.find_property(owner, member))
        return emit_api_return(type_from_godot_api(property->type), std::move(result));
    return result;
}

std::string CodeGenerator::emit_direct_builtin_assignment(std::string_view owner,
                                                          std::string object,
                                                          std::string_view member,
                                                          std::string value) const {
    const auto component_index = [](std::string_view name) -> int {
        if (name == "x")
            return 0;
        if (name == "y")
            return 1;
        if (name == "z")
            return 2;
        return name == "w" ? 3 : -1;
    };
    const auto index = component_index(member);
    if (owner == "Basis" && index >= 0)
        return object + ".set_column(" + std::to_string(index) + ", " + value + ")";
    if (owner == "Projection" && index >= 0)
        return object + "[" + std::to_string(index) + "] = " + value;
    if (owner == "Transform2D" && member == "origin")
        return object + ".set_origin(" + value + ")";
    if ((owner == "Rect2" || owner == "Rect2i" || owner == "AABB") && member == "end")
        return object + ".set_end(" + value + ")";
    return object + "." + sanitize_identifier(std::string{member}) + " = " + value;
}

std::string CodeGenerator::emit_dynamic_assignment(const ir::Statement& statement,
                                                   std::size_t indentation) const {
    const auto& target = *statement.condition;
    const auto prefix = indent(indentation);
    const auto nested_prefix = indent(indentation + 1);
    const auto suffix = std::to_string(temporary_counter_++);

    // A Variant access may cross value types such as Vector2, Color or Transform3D. Godot returns
    // those values by copy, so changing only the leaf silently loses the mutation. Preserve the
    // complete lvalue chain and write each changed child back into its parent in reverse order.
    std::vector<const ir::Expression*> access_chain;
    const ir::Expression* root = &target;
    while (
        (root->kind == ir::ExpressionKind::member || root->kind == ir::ExpressionKind::subscript) &&
        !root->operands.empty()) {
        access_chain.push_back(root);
        root = root->operands.front().get();
    }
    std::reverse(access_chain.begin(), access_chain.end());
    if (access_chain.empty()) {
        diagnostics_.error("GDS3007", "dynamic assignment is missing an access chain", target.span);
        return prefix + "/* invalid dynamic assignment */;\n";
    }

    const auto root_name = "_gdpp_dynamic_root_" + suffix;
    std::string result = prefix + "{\n" + nested_prefix + "godot::Variant " + root_name + " = " +
                         emit_expression(*root) + ";\n";
    std::vector<std::string> containers{root_name};
    std::vector<std::string> keys(access_chain.size());

    for (std::size_t index = 0; index + 1 < access_chain.size(); ++index) {
        const auto* access = access_chain[index];
        const auto child_name = "_gdpp_dynamic_child_" + suffix + "_" + std::to_string(index);
        if (access->kind == ir::ExpressionKind::subscript) {
            keys[index] = "_gdpp_dynamic_key_" + suffix + "_" + std::to_string(index);
            result += nested_prefix + "const godot::Variant " + keys[index] + " = " +
                      emit_expression(*access->operands.at(1)) + ";\n" + nested_prefix +
                      "godot::Variant " + child_name + " = gdpp::runtime::get_key(" +
                      containers.back() + ", " + keys[index] + ");\n";
        } else {
            result += nested_prefix + "godot::Variant " + child_name +
                      " = gdpp::runtime::get_named(" + containers.back() + ", " +
                      godot_string_name(access->value) + ");\n";
        }
        containers.push_back(child_name);
    }

    const auto* leaf = access_chain.back();
    if (leaf->kind == ir::ExpressionKind::subscript) {
        const auto leaf_index = access_chain.size() - 1;
        keys[leaf_index] = "_gdpp_dynamic_key_" + suffix + "_" + std::to_string(leaf_index);
        result += nested_prefix + "const godot::Variant " + keys[leaf_index] + " = " +
                  emit_expression(*leaf->operands.at(1)) + ";\n";
    }

    const bool compound = statement.operation != "=";
    const auto value_name = "_gdpp_dynamic_value_" + suffix;
    std::string current_name;
    if (compound) {
        current_name = "_gdpp_dynamic_current_" + suffix;
        result += nested_prefix + "const godot::Variant " + current_name + " = ";
        if (leaf->kind == ir::ExpressionKind::subscript) {
            result += "gdpp::runtime::get_key(" + containers.back() + ", " + keys.back() + ");\n";
        } else {
            result += "gdpp::runtime::get_named(" + containers.back() + ", " +
                      godot_string_name(leaf->value) + ");\n";
        }
    }
    result += nested_prefix + "const godot::Variant " + value_name + " = " +
              emit_expression(*statement.expression) + ";\n";
    std::string assigned_value = value_name;
    if (compound) {
        const auto operation = statement.operation.substr(0, statement.operation.size() - 1);
        assigned_value = "gdpp::runtime::binary(godot::Variant::" + variant_operator(operation) +
                         ", " + current_name + ", " + value_name + ")";
    }

    if (leaf->kind == ir::ExpressionKind::subscript) {
        result += nested_prefix + "gdpp::runtime::set_key(" + containers.back() + ", " +
                  keys.back() + ", " + assigned_value + ");\n";
    } else {
        result += nested_prefix + "gdpp::runtime::set_named(" + containers.back() + ", " +
                  godot_string_name(leaf->value) + ", " + assigned_value + ");\n";
    }

    for (std::size_t child_index = containers.size() - 1; child_index > 0; --child_index) {
        const auto access_index = child_index - 1;
        const auto* access = access_chain[access_index];
        if (access->kind == ir::ExpressionKind::subscript) {
            result += nested_prefix + "gdpp::runtime::set_key(" + containers[child_index - 1] +
                      ", " + keys[access_index] + ", " + containers[child_index] + ");\n";
        } else {
            result += nested_prefix + "gdpp::runtime::set_named(" + containers[child_index - 1] +
                      ", " + godot_string_name(access->value) + ", " + containers[child_index] +
                      ");\n";
        }
    }

    // Objects, Array and Dictionary carry reference semantics through Variant. Other roots are
    // values and need one final assignment so copy-on-write builtins and Variant-held values live.
    const bool root_requires_writeback =
        root->kind == ir::ExpressionKind::identifier && root->type.kind != TypeKind::object &&
        root->type.kind != TypeKind::array && root->type.kind != TypeKind::dictionary &&
        root->type.kind != TypeKind::script_resource;
    if (root_requires_writeback) {
        auto converted = emit_conversion(root->type, {TypeKind::variant, "Variant"}, root_name);
        if (root->resolution == ir::ResolutionKind::script_property) {
            result += nested_prefix + root->setter + "(" + converted + ");\n";
        } else if (root->resolution == ir::ResolutionKind::godot_property && !root->direct_access &&
                   !root->setter.empty()) {
            std::string index;
            const auto* setter = api_.find_method(root->resolved_owner, root->setter);
            if (root->indexed_argument >= 0) {
                index = std::to_string(root->indexed_argument);
                if (setter) {
                    if (const auto* argument = api_.argument(*setter, 0))
                        index = emit_api_argument(argument->type, argument->meta,
                                                  {TypeKind::integer, "int"}, std::move(index));
                }
                index += ", ";
            }
            if (setter) {
                const auto value_index = root->indexed_argument >= 0 ? 1U : 0U;
                if (const auto* argument = api_.argument(*setter, value_index))
                    converted = emit_api_argument(argument->type, argument->meta, root->type,
                                                  std::move(converted));
            }
            result += nested_prefix + root->setter + "(" + index + converted + ");\n";
        } else {
            result += nested_prefix + emit_expression(*root) + " = " + converted + ";\n";
        }
    }
    return result + prefix + "}\n";
}

std::string CodeGenerator::emit_dictionary_member_assignment(const ir::Statement& statement,
                                                             const std::size_t indentation) const {
    const auto& target = *statement.condition;
    const auto prefix = indent(indentation);
    const auto nested_prefix = indent(indentation + 1);
    const auto suffix = std::to_string(temporary_counter_++);
    const auto dictionary = "_gdpp_dictionary_target_" + suffix;
    const auto key = "_gdpp_dictionary_key_" + suffix;
    const auto value = "_gdpp_dictionary_value_" + suffix;

    std::string result = prefix + "{\n" + nested_prefix + "auto &&" + dictionary + " = " +
                         emit_expression(*target.operands.at(0)) + ";\n" + nested_prefix +
                         "static const godot::Variant " + key + " = " +
                         godot_string_name(target.value) + ";\n";
    if (statement.operation != "=") {
        const auto slot = "_gdpp_dictionary_slot_" + suffix;
        result +=
            nested_prefix + "godot::Variant &" + slot + " = " + dictionary + "[" + key + "];\n";
        result += nested_prefix + "const auto " + value + " = " +
                  emit_expression(*statement.expression) + ";\n";
        const auto operation = statement.operation.substr(0, statement.operation.size() - 1);
        if (statement.expression->type.kind == TypeKind::integer ||
            statement.expression->type.kind == TypeKind::enumeration) {
            result += nested_prefix + "gdpp::runtime::compound_assign_integer(" + slot +
                      ", godot::Variant::" + variant_operator(operation) + ", " + value + ");\n";
        } else {
            result += nested_prefix + "gdpp::runtime::compound_assign(" + slot +
                      ", godot::Variant::" + variant_operator(operation) + ", " + value + ");\n";
        }
    } else {
        result += nested_prefix + "const godot::Variant " + value + " = " +
                  emit_expression(*statement.expression) + ";\n" + nested_prefix + dictionary +
                  "[" + key + "] = " + value + ";\n";
    }
    return result + prefix + "}\n";
}

std::string CodeGenerator::emit_truthy(const ir::Expression& expression) const {
    auto value = emit_expression(expression);
    if (expression.type.kind == TypeKind::boolean)
        return "static_cast<bool>(" + value + ")";
    if (expression.type.kind == TypeKind::object) {
        if (script_symbols_ && script_symbols_->find_external(expression.type.name))
            return "static_cast<bool>(godot::Variant(" + value + "))";
        bool ref_counted = api_.inherits(expression.type.name, "RefCounted") ||
                           !inner_cpp_type(expression.type.name).empty();
        if (!ref_counted && script_symbols_) {
            const auto* symbol = script_symbols_->find_class(expression.type.name);
            if (!symbol && current_script_ && current_script_->script_name == expression.type.name)
                symbol = current_script_;
            ref_counted = symbol && api_.inherits(symbol->godot_base_type, "RefCounted");
        }
        return ref_counted ? "(" + value + ").is_valid()" : "(" + value + " != nullptr)";
    }
    return "static_cast<bool>(godot::Variant(" + value + "))";
}

std::string CodeGenerator::emit_expression(const ir::Expression& expression) const {
    switch (expression.kind) {
    case ir::ExpressionKind::literal: {
        if (expression.literal_kind == ir::LiteralKind::boolean)
            return expression.value;
        if (expression.literal_kind == ir::LiteralKind::nil)
            return "godot::Variant()";
        if (expression.literal_kind == ir::LiteralKind::integer) {
            if (expression.value == "9223372036854775808" ||
                expression.value == "-9223372036854775808") {
                return "(-static_cast<int64_t>(9223372036854775807LL) - "
                       "static_cast<int64_t>(1))";
            }
            return "static_cast<int64_t>(" + expression.value + ")";
        }
        if (expression.literal_kind == ir::LiteralKind::floating) {
            if (expression.value == "inf")
                return "Math_INF";
            if (expression.value == "-inf")
                return "-Math_INF";
            if (expression.value == "nan" || expression.value == "-nan")
                return "Math_NAN";
            return expression.value;
        }
        if (expression.literal_kind == ir::LiteralKind::string_name)
            return godot_string_name(expression.value);
        if (expression.literal_kind == ir::LiteralKind::node_path)
            return godot_node_path(expression.value);
        return godot_string(expression.value);
    }
    case ir::ExpressionKind::node_reference: {
        auto path = expression.value;
        if (!path.empty() && path.front() == '$')
            path.erase(path.begin());
        return "godot::Variant(get_node<godot::Node>(" + godot_node_path(path) + "))";
    }
    case ir::ExpressionKind::identifier:
        if (expression.resolution == ir::ResolutionKind::none) {
            if (const auto override = local_expression_overrides_.find(expression.value);
                override != local_expression_overrides_.end()) {
                return override->second;
            }
        }
        if (expression.resolution == ir::ResolutionKind::godot_constructor)
            return cpp_type(expression.type);
        if (expression.resolution == ir::ResolutionKind::godot_singleton)
            return "godot::" + expression.resolved_owner + "::get_singleton()";
        if (expression.resolution == ir::ResolutionKind::external_singleton)
            return "gdpp::runtime::find_engine_singleton(" + godot_string_name(expression.value) +
                   ")";
        if (expression.resolution == ir::ResolutionKind::godot_type)
            return expression.type.kind == TypeKind::object ? "godot::" + expression.resolved_owner
                                                            : cpp_type(expression.type);
        if (expression.resolution == ir::ResolutionKind::external_type)
            return godot_string_name(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::script_type)
            return godot_string_name(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::inner_type)
            return inner_cpp_type(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::script_super)
            return native_super_owner(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::script_signal)
            return "godot::Signal(this, " + godot_string_name(expression.value) + ")";
        if (expression.resolution == ir::ResolutionKind::script_autoload)
            return "godot::Object::cast_to<" + expression.resolved_owner +
                   ">(gdpp::runtime::find_autoload(" + godot_string_name(expression.getter) + "))";
        if (expression.resolution == ir::ResolutionKind::script_callable)
            return "godot::Callable(this, " + godot_string_name(expression.value) + ")";
        if (expression.resolution == ir::ResolutionKind::script_static_callable) {
            const auto found = local_functions_.find(expression.value);
            if (found == local_functions_.end() || current_native_class_name_.empty()) {
                diagnostics_.error("GDS3010", "static callable metadata is unavailable",
                                   expression.span);
                return "godot::Callable()";
            }
            const auto& function = *found->second;
            const auto required = static_cast<std::size_t>(
                std::count_if(function.parameters.begin(), function.parameters.end(),
                              [](const auto& parameter) { return !parameter.default_value; }));
            std::string result = "gdpp::runtime::make_callable(nullptr, " +
                                 std::to_string(required) + ", " +
                                 std::to_string(function.parameters.size()) +
                                 ", [](const godot::Array& _gdpp_static_arguments) -> "
                                 "godot::Variant { ";
            if (function.return_type.kind != TypeKind::void_type)
                result += "return godot::Variant(";
            result += current_native_class_name_ + "::" + sanitize_identifier(function.name) + "(";
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    result += ", ";
                const auto& parameter = function.parameters[index];
                const auto indexed = "_gdpp_static_arguments[" + std::to_string(index) + "]";
                if (parameter.default_value) {
                    result += "_gdpp_static_arguments.size() > " + std::to_string(index) +
                              " ? godot::Variant(" + indexed +
                              ") : gdpp::runtime::default_argument()";
                } else {
                    result +=
                        emit_conversion(parameter.type, {TypeKind::variant, "Variant"}, indexed);
                }
            }
            result += ")";
            if (function.return_type.kind != TypeKind::void_type)
                result += ")";
            else
                result += "; return godot::Variant()";
            return result + "; })";
        }
        if (expression.resolution == ir::ResolutionKind::script_static_field)
            return "_gdpp_static_" + sanitize_identifier(expression.value) + "_storage()";
        if (expression.resolution == ir::ResolutionKind::script_enum_type &&
            !expression.resolved_owner.empty())
            return sanitize_qualified_identifier(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::enum_member)
            return enum_identifier(expression.value);
        if (expression.resolution == ir::ResolutionKind::script_constant &&
            managed_constant_reference(expression)) {
            return sanitize_identifier(expression.value) + "()";
        }
        if (expression.resolution == ir::ResolutionKind::godot_property &&
            !expression.direct_access && !expression.getter.empty()) {
            std::string index;
            if (expression.indexed_argument >= 0) {
                index = std::to_string(expression.indexed_argument);
                if (const auto* getter =
                        api_.find_method(expression.resolved_owner, expression.getter)) {
                    if (const auto* argument = api_.argument(*getter, 0))
                        index = emit_api_argument(argument->type, argument->meta,
                                                  {TypeKind::integer, "int"}, std::move(index));
                }
            }
            return emit_api_return(expression.type, expression.getter + "(" + index + ")");
        }
        if (expression.resolution == ir::ResolutionKind::script_property)
            return expression.getter + "()";
        if (expression.resolution == ir::ResolutionKind::global_constant ||
            expression.resolution == ir::ResolutionKind::global_enum_value ||
            expression.resolution == ir::ResolutionKind::global_enum_type)
            return expression.resolved_owner;
        if (current_script_ && local_functions_.find(expression.value) != local_functions_.end()) {
            const auto member =
                std::find_if(current_script_->members.begin(), current_script_->members.end(),
                             [&](const auto& candidate) {
                                 return candidate.kind == ScriptMemberKind::function &&
                                        candidate.name == expression.value;
                             });
            if (member != current_script_->members.end())
                return script_method_native_name(*current_script_, *member);
        }
        return expression.value == "self" ? "this" : sanitize_identifier(expression.value);
    case ir::ExpressionKind::unary: {
        if (expression.value == "-" &&
            expression.operands.at(0)->kind == ir::ExpressionKind::literal &&
            expression.operands.at(0)->literal_kind == ir::LiteralKind::integer &&
            expression.operands.at(0)->value == "9223372036854775808")
            return emit_expression(*expression.operands.at(0));
        const auto& operand_type = expression.operands.at(0)->type;
        const bool has_direct_cpp_operator =
            (expression.value == "+" || expression.value == "-") && operand_type.is_numeric();
        const bool has_direct_cpp_not =
            expression.value == "not" &&
            (operand_type.kind == TypeKind::boolean || operand_type.kind == TypeKind::object);
        if (operand_type.is_dynamic() || (!has_direct_cpp_operator && !has_direct_cpp_not)) {
            const auto operation_name = expression.value == "+" ? "OP_POSITIVE"
                                        : expression.value == "-"
                                            ? "OP_NEGATE"
                                            : variant_operator(expression.value);
            auto evaluated = "gdpp::runtime::unary(godot::Variant::" + operation_name + ", " +
                             emit_expression(*expression.operands.at(0)) + ")";
            return operand_type.is_dynamic()
                       ? std::move(evaluated)
                       : emit_conversion(expression.type, {TypeKind::variant, "Variant"},
                                         std::move(evaluated));
        }
        if (expression.value == "not" && operand_type.kind == TypeKind::object) {
            if (script_symbols_ && script_symbols_->find_external(operand_type.name)) {
                return "(!static_cast<bool>(godot::Variant(" +
                       emit_expression(*expression.operands.at(0)) + ")))";
            }
            auto ref_counted = api_.inherits(operand_type.name, "RefCounted") ||
                               !inner_cpp_type(operand_type.name).empty();
            if (!ref_counted && script_symbols_) {
                if (const auto* symbol = script_symbols_->find_global(operand_type.name))
                    ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
            }
            const auto operand = emit_expression(*expression.operands.at(0));
            return ref_counted ? "(!(" + operand + ").is_valid())" : "(!" + operand + ")";
        }
        const auto operation = expression.value == "not" ? "!" : expression.value;
        return "(" + operation + emit_expression(*expression.operands.at(0)) + ")";
    }
    case ir::ExpressionKind::await_expression:
        diagnostics_.error("GDS3006", "unlowered await expression reached code generation",
                           expression.span);
        return "godot::Variant()";
    case ir::ExpressionKind::binary: {
        if ((expression.value == "and" || expression.value == "or") &&
            !expression.operands.at(0)->type.is_dynamic() &&
            !expression.operands.at(1)->type.is_dynamic()) {
            return "(" + emit_truthy(*expression.operands.at(0)) +
                   (expression.value == "and" ? " && " : " || ") +
                   emit_truthy(*expression.operands.at(1)) + ")";
        }
        auto operation = expression.value;
        if (operation == "and")
            operation = "&&";
        if (operation == "or")
            operation = "||";
        if (operation == "in" || operation == "not in") {
            const auto membership =
                "static_cast<bool>(gdpp::runtime::binary(godot::Variant::OP_IN, " +
                emit_expression(*expression.operands.at(0)) + ", " +
                emit_expression(*expression.operands.at(1)) + "))";
            return operation == "not in" ? "!(" + membership + ")" : membership;
        }
        if (operation == "is" || operation == "is not") {
            const bool negate = operation == "is not";
            const auto type_test = [negate](std::string value) {
                return negate ? "!(" + value + ")" : value;
            };
            const auto& value = *expression.operands.at(0);
            const auto& target = *expression.operands.at(1);
            const auto emitted_value = emit_expression(value);
            if (target.resolution != ir::ResolutionKind::godot_type &&
                target.resolution != ir::ResolutionKind::external_type &&
                target.resolution != ir::ResolutionKind::script_type &&
                target.resolution != ir::ResolutionKind::inner_type) {
                diagnostics_.error("GDS3001", "type test is missing its resolved target type",
                                   expression.span);
                return type_test("false");
            }
            if (target.type.kind == TypeKind::object) {
                if (target.resolution == ir::ResolutionKind::external_type) {
                    return type_test("gdpp::runtime::is_external_instance(godot::Variant(" +
                                     emitted_value + "), " +
                                     godot_string_name(target.resolved_owner) + ")");
                }
                const auto target_cpp = target.resolution == ir::ResolutionKind::script_type
                                            ? target.resolved_owner
                                        : target.resolution == ir::ResolutionKind::inner_type
                                            ? inner_cpp_type(target.resolved_owner)
                                            : "godot::" + target.resolved_owner;
                std::string object_value;
                if (value.type.kind == TypeKind::nil) {
                    object_value = "nullptr";
                } else if (value.type.is_dynamic()) {
                    object_value = "(" + emitted_value + ").get_validated_object()";
                } else if (value.type.kind != TypeKind::object) {
                    return type_test("(static_cast<void>(" + emitted_value + "), false)");
                } else {
                    object_value = emitted_value;
                    const bool external_value =
                        script_symbols_ && script_symbols_->find_external(value.type.name);
                    if (external_value)
                        object_value =
                            "(godot::Variant(" + object_value + ")).get_validated_object()";
                    bool ref_counted = api_.inherits(value.type.name, "RefCounted") ||
                                       !inner_cpp_type(value.type.name).empty();
                    if (!ref_counted && script_symbols_) {
                        if (const auto* symbol = script_symbols_->find_global(value.type.name)) {
                            ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
                        }
                    }
                    if (ref_counted && !external_value && emitted_value != "this")
                        object_value = "(" + object_value + ").ptr()";
                }
                return type_test("(godot::Object::cast_to<" + target_cpp + ">(" + object_value +
                                 ") != nullptr)");
            }
            if (target.type.kind == TypeKind::variant) {
                return type_test("(static_cast<void>(" + emitted_value + "), true)");
            }
            const auto expected_variant_type = variant_type(target.type);
            if (expected_variant_type == "godot::Variant::NIL") {
                diagnostics_.error("GDS3001", "unsupported value type in 'is' expression",
                                   target.span);
                return type_test("false");
            }
            if (value.type.is_dynamic()) {
                return type_test("((" + emitted_value + ").get_type() == " + expected_variant_type +
                                 ")");
            }
            const auto matches = variant_type(value.type) == expected_variant_type;
            return type_test("(static_cast<void>(" + emitted_value + "), " +
                             std::string{matches ? "true" : "false"} + ")");
        }
        if (operation == "as") {
            const auto& value = *expression.operands.at(0);
            const auto& target = *expression.operands.at(1);
            const auto emitted_value = emit_expression(value);
            if (target.resolution == ir::ResolutionKind::external_type) {
                const auto suffix = std::to_string(temporary_counter_++);
                const auto temporary = "_gdpp_external_cast_" + suffix;
                return "([&]() -> godot::Variant { const godot::Variant " + temporary + " = " +
                       emitted_value + "; return gdpp::runtime::is_external_instance(" + temporary +
                       ", " + godot_string_name(target.resolved_owner) + ") ? " + temporary +
                       " : godot::Variant(); }())";
            }
            if (target.type.kind == TypeKind::object &&
                (target.resolution == ir::ResolutionKind::godot_type ||
                 target.resolution == ir::ResolutionKind::script_type ||
                 target.resolution == ir::ResolutionKind::inner_type)) {
                const auto target_cpp = target.resolution == ir::ResolutionKind::script_type
                                            ? target.resolved_owner
                                        : target.resolution == ir::ResolutionKind::inner_type
                                            ? inner_cpp_type(target.resolved_owner)
                                            : "godot::" + target.resolved_owner;
                auto object = value.type.is_dynamic()
                                  ? "(" + emitted_value + ").get_validated_object()"
                                  : emitted_value;
                if (!value.type.is_dynamic() && value.type.kind == TypeKind::object) {
                    bool ref_counted = api_.inherits(value.type.name, "RefCounted") ||
                                       !inner_cpp_type(value.type.name).empty();
                    if (!ref_counted && script_symbols_) {
                        if (const auto* symbol = script_symbols_->find_class(value.type.name)) {
                            ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
                        }
                    }
                    if (ref_counted && emitted_value != "this")
                        object = "(" + object + ").ptr()";
                }
                return "godot::Object::cast_to<" + target_cpp + ">(" + object + ")";
            }
            return "static_cast<" + cpp_type(expression.type) + ">(godot::Variant(" +
                   emitted_value + "))";
        }
        if (operation == "**") {
            return "static_cast<" + cpp_type(expression.type) +
                   ">(gdpp::runtime::binary("
                   "godot::Variant::OP_POWER, " +
                   emit_expression(*expression.operands.at(0)) + ", " +
                   emit_expression(*expression.operands.at(1)) + "))";
        }
        const auto& left_type = expression.operands.at(0)->type;
        const auto& right_type = expression.operands.at(1)->type;
        if ((operation == "==" || operation == "!=") &&
            ((left_type.kind == TypeKind::object && right_type.kind == TypeKind::nil) ||
             (left_type.kind == TypeKind::nil && right_type.kind == TypeKind::object))) {
            const auto& object = left_type.kind == TypeKind::object ? *expression.operands.at(0)
                                                                    : *expression.operands.at(1);
            if (script_symbols_ && script_symbols_->find_external(object.type.name)) {
                const auto equality =
                    "static_cast<bool>(gdpp::runtime::binary(godot::Variant::OP_EQUAL, " +
                    emit_expression(object) + ", godot::Variant()))";
                return operation == "==" ? "(" + equality + ")" : "(!(" + equality + "))";
            }
            auto ref_counted = api_.inherits(object.type.name, "RefCounted") ||
                               !inner_cpp_type(object.type.name).empty();
            if (!ref_counted && script_symbols_) {
                if (const auto* symbol = script_symbols_->find_global(object.type.name))
                    ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
            }
            const auto null_test = ref_counted ? emit_expression(object) + ".is_null()"
                                               : emit_expression(object) + " == nullptr";
            return operation == "==" ? "(" + null_test + ")" : "(!(" + null_test + "))";
        }
        const bool external_operand =
            script_symbols_ && ((left_type.kind == TypeKind::object &&
                                 script_symbols_->find_external(left_type.name)) ||
                                (right_type.kind == TypeKind::object &&
                                 script_symbols_->find_external(right_type.name)));
        if (left_type.is_dynamic() || right_type.is_dynamic() || external_operand) {
            if (expression.value == "and" || expression.value == "or") {
                const auto suffix = std::to_string(temporary_counter_++);
                const auto left = "_gdpp_logic_left_" + suffix;
                std::string result = "([&]() -> bool { const godot::Variant " + left + " = " +
                                     emit_expression(*expression.operands.at(0)) + "; ";
                if (expression.value == "and")
                    result += "if (!static_cast<bool>(" + left + ")) return false; ";
                else
                    result += "if (static_cast<bool>(" + left + ")) return true; ";
                return result + "return static_cast<bool>(godot::Variant(" +
                       emit_expression(*expression.operands.at(1)) + ")); }())";
            }
            std::string runtime_function{"gdpp::runtime::binary"};
            if ((left_type.is_dynamic() && right_type.kind == TypeKind::integer) ||
                (right_type.is_dynamic() && left_type.kind == TypeKind::integer)) {
                runtime_function = "gdpp::runtime::binary_integer";
            }
            const auto dynamic = runtime_function +
                                 "(godot::Variant::" + variant_operator(expression.value) + ", " +
                                 emit_expression(*expression.operands.at(0)) + ", " +
                                 emit_expression(*expression.operands.at(1)) + ")";
            return expression.type.kind == TypeKind::boolean ? "static_cast<bool>(" + dynamic + ")"
                                                             : dynamic;
        }
        if (left_type.kind == TypeKind::builtin || right_type.kind == TypeKind::builtin) {
            const auto evaluated =
                "gdpp::runtime::binary(godot::Variant::" + variant_operator(expression.value) +
                ", " + emit_expression(*expression.operands.at(0)) + ", " +
                emit_expression(*expression.operands.at(1)) + ")";
            return emit_conversion(expression.type, {TypeKind::variant, "Variant"}, evaluated);
        }
        return "(" + emit_expression(*expression.operands.at(0)) + " " + operation + " " +
               emit_expression(*expression.operands.at(1)) + ")";
    }
    case ir::ExpressionKind::conditional: {
        const auto emit_branch = [&](const ir::Expression& branch) {
            auto value = emit_expression(branch);
            if (expression.type.is_dynamic())
                return "godot::Variant(" + value + ")";
            return emit_conversion(expression.type, branch.type, std::move(value));
        };
        return "(" + emit_truthy(*expression.operands.at(1)) + " ? " +
               emit_branch(*expression.operands.at(0)) + " : " +
               emit_branch(*expression.operands.at(2)) + ")";
    }
    case ir::ExpressionKind::call: {
        if (expression.resolution == ir::ResolutionKind::script_resource)
            return cpp_type(expression.type) + "{}";
        const auto& callee = *expression.operands.at(0);
        if (callee.resolution == ir::ResolutionKind::external_constructor) {
            return "gdpp::runtime::instantiate_external_class(" +
                   godot_string_name(callee.resolved_owner) + ")";
        }
        if (callee.resolution == ir::ResolutionKind::external_static_method) {
            const auto suffix = std::to_string(temporary_counter_++);
            std::string invocation = "([&]() -> godot::Variant { ";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                invocation += "const godot::Variant _gdpp_static_argument_" + suffix + "_" +
                              std::to_string(index - 1) + " = " +
                              emit_expression(*expression.operands[index]) + "; ";
            }
            invocation += "return gdpp::runtime::call_external_static(" +
                          godot_string_name(callee.resolved_owner) + ", " +
                          godot_string_name(callee.value);
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                invocation += ", _gdpp_static_argument_" + suffix + "_" + std::to_string(index - 1);
            }
            invocation += "); }())";
            return expression.type.is_dynamic()
                       ? invocation
                       : emit_conversion(expression.type, {TypeKind::variant, "Variant"},
                                         invocation);
        }
        if (callee.resolution == ir::ResolutionKind::godot_constructor &&
            callee.type.kind == TypeKind::object) {
            const auto native_type = "godot::" + callee.resolved_owner;
            if (api_.inherits(callee.resolved_owner, "RefCounted"))
                return cpp_type(callee.type) + "(memnew(" + native_type + "))";
            return "memnew(" + native_type + ")";
        }
        if (callee.resolution == ir::ResolutionKind::godot_constructor &&
            expression.operands.size() == 2 &&
            (expression.type.kind == TypeKind::boolean ||
             expression.type.kind == TypeKind::integer ||
             expression.type.kind == TypeKind::floating ||
             expression.type.kind == TypeKind::string ||
             expression.type.kind == TypeKind::string_name)) {
            return emit_conversion(expression.type, expression.operands.at(1)->type,
                                   emit_expression(*expression.operands.at(1)));
        }
        const auto* godot_method =
            callee.resolution == ir::ResolutionKind::godot_method
                ? api_.find_method(callee.resolved_owner, callee.value)
            : callee.resolution == ir::ResolutionKind::script_super && !callee.getter.empty()
                ? api_.find_method(callee.getter,
                                   callee.setter.empty() ? callee.value : callee.setter)
                : nullptr;
        const auto* godot_constructor =
            callee.resolution == ir::ResolutionKind::godot_constructor &&
                    callee.indexed_argument >= 0
                ? api_.find_constructor(callee.resolved_owner, expression.operands.size() - 1,
                                        static_cast<std::size_t>(callee.indexed_argument))
                : nullptr;
        const ScriptMemberSymbol* script_method = nullptr;
        const ScriptClassSymbol* script_method_owner = nullptr;
        if (script_symbols_) {
            const ScriptClassSymbol* owner = nullptr;
            if (callee.kind == ir::ExpressionKind::identifier) {
                owner = callee.resolution == ir::ResolutionKind::script_super && current_script_
                            ? script_symbols_->base_of(*current_script_)
                            : current_script_;
            } else if (callee.kind == ir::ExpressionKind::member && !callee.operands.empty()) {
                if (callee.resolution == ir::ResolutionKind::script_super && current_script_)
                    owner = script_symbols_->base_of(*current_script_);
                if (!owner)
                    owner = script_symbols_->find_global(callee.operands.at(0)->type.name);
                if (!owner && callee.operands.at(0)->value == "self")
                    owner = current_script_;
            }
            if (owner) {
                const auto* member = script_symbols_->find_member(
                    *owner,
                    callee.resolution == ir::ResolutionKind::script_super && !callee.setter.empty()
                        ? callee.setter
                        : callee.value);
                if (member && member->kind == ScriptMemberKind::function) {
                    script_method = member;
                    const ScriptClassSymbol* declaration_owner = owner;
                    while (declaration_owner) {
                        const auto declared = std::find_if(
                            declaration_owner->members.begin(), declaration_owner->members.end(),
                            [&](const auto& candidate) {
                                return candidate.kind == ScriptMemberKind::function &&
                                       candidate.name == member->name;
                            });
                        if (declared != declaration_owner->members.end()) {
                            script_method_owner = declaration_owner;
                            script_method = &*declared;
                            break;
                        }
                        declaration_owner = script_symbols_->base_of(*declaration_owner);
                    }
                }
            }
        }
        const auto script_native_name =
            script_method && script_method_owner
                ? script_method_native_name(*script_method_owner, *script_method)
                : sanitize_identifier(callee.value);
        const std::vector<Type>* local_parameters = nullptr;
        if (!script_method &&
            (callee.kind == ir::ExpressionKind::identifier ||
             (callee.kind == ir::ExpressionKind::member && !callee.operands.empty() &&
              (callee.operands.at(0)->value == "self" ||
               callee.operands.at(0)->type.name == (current_inner_script_
                                                        ? current_inner_script_->name
                                                    : current_script_ ? current_script_->script_name
                                                                      : std::string{}))))) {
            if (const auto found = local_function_parameters_.find(callee.value);
                found != local_function_parameters_.end()) {
                local_parameters = &found->second;
            }
        }
        const auto emit_call_argument = [&](std::size_t operand_index,
                                            std::string value) -> std::string {
            if (godot_constructor) {
                if (const auto* argument = api_.argument(*godot_constructor, operand_index - 1)) {
                    return emit_api_argument(argument->type, argument->meta,
                                             expression.operands[operand_index]->type,
                                             std::move(value));
                }
            }
            if (godot_method) {
                if (const auto* argument = api_.argument(*godot_method, operand_index - 1)) {
                    return emit_api_argument(argument->type, argument->meta,
                                             expression.operands[operand_index]->type,
                                             std::move(value));
                }
            }
            if (script_method && operand_index - 1 < script_method->parameters.size()) {
                return emit_conversion(script_method->parameters[operand_index - 1],
                                       expression.operands[operand_index]->type, std::move(value));
            }
            if (local_parameters && operand_index - 1 < local_parameters->size()) {
                return emit_conversion((*local_parameters)[operand_index - 1],
                                       expression.operands[operand_index]->type, std::move(value));
            }
            return value;
        };
        const auto append_required_variant_defaults = [&](std::string& call, std::size_t provided) {
            const bool needs_null_default = callee.resolved_owner == "Dictionary" &&
                                            provided == 1 &&
                                            (callee.value == "get" || callee.value == "get_or_add");
            const bool array_reduce_default =
                callee.resolved_owner == "Array" && callee.value == "reduce" && provided == 1;
            if (needs_null_default || array_reduce_default)
                call += (provided == 0 ? "godot::Variant()" : ", godot::Variant()");
        };
        if (callee.resolution == ir::ResolutionKind::utility_function ||
            callee.resolution == ir::ResolutionKind::intrinsic) {
            const bool is_intrinsic = callee.resolution == ir::ResolutionKind::intrinsic;
            if (is_intrinsic &&
                (callee.intrinsic == IntrinsicKind::load ||
                 callee.intrinsic == IntrinsicKind::preload) &&
                !expression.type.is_dynamic()) {
                return emit_conversion(expression.type, {TypeKind::variant, "Variant"},
                                       "gdpp::runtime::load_resource(" +
                                           emit_expression(*expression.operands.at(1)) + ")");
            }
            const auto suffix = std::to_string(temporary_counter_++);
            const auto function_name =
                callee.resolved_owner == "typeof" ? std::string{"type_of"} : callee.resolved_owner;
            std::string invocation;
            if (is_intrinsic) {
                const auto* feature = IntrinsicRegistry::latest().find(callee.intrinsic);
                invocation = feature ? std::string{feature->runtime_symbol} : std::string{};
                if (invocation.empty()) {
                    diagnostics_.error("GDS5016", "intrinsic has no registered C++ lowering",
                                       expression.span);
                    return "godot::Variant()";
                }
            } else {
                invocation = callee.resolved_owner == "is_instance_valid"
                                 ? std::string{"gdpp::runtime::is_instance_valid"}
                                 : "godot::UtilityFunctions::" + function_name;
            }
            const auto* utility_function =
                is_intrinsic ? nullptr : api_.find_utility_function(callee.resolved_owner);
            const bool zero_arity_vararg = utility_function && utility_function->is_vararg &&
                                           utility_function->required_arguments == 0 &&
                                           expression.operands.size() == 1;
            // godot-cpp exposes a mandatory placeholder parameter for vararg wrappers even when
            // Godot accepts zero arguments. An empty String preserves the observable output of
            // print-family calls; str() is exactly an empty String and needs no engine call.
            if (zero_arity_vararg && callee.resolved_owner == "str")
                return "godot::String()";
            if (!in_function_body_) {
                std::string direct = invocation + "(";
                for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                    if (index > 1)
                        direct += ", ";
                    auto argument = emit_expression(*expression.operands[index]);
                    if (!is_intrinsic) {
                        if (const auto* function =
                                api_.find_utility_function(callee.resolved_owner)) {
                            if (const auto* record = api_.argument(*function, index - 1)) {
                                argument = emit_api_argument(record->type, record->meta,
                                                             expression.operands[index]->type,
                                                             std::move(argument));
                            }
                        }
                    }
                    direct += argument;
                }
                if (zero_arity_vararg)
                    direct += "godot::String()";
                return is_intrinsic ? direct + ")" : emit_api_return(expression.type, direct + ")");
            }
            std::string result = "([&]()";
            if (expression.type.kind != TypeKind::void_type)
                result += " -> " + cpp_type(expression.type);
            result += " { ";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                result += "const auto _gdpp_utility_argument_" + suffix + "_" +
                          std::to_string(index - 1) + " = " +
                          emit_expression(*expression.operands[index]) + "; ";
            }
            std::string call = invocation + "(";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                if (index > 1)
                    call += ", ";
                auto argument =
                    "_gdpp_utility_argument_" + suffix + "_" + std::to_string(index - 1);
                if (!is_intrinsic) {
                    if (const auto* function = api_.find_utility_function(callee.resolved_owner)) {
                        if (const auto* record = api_.argument(*function, index - 1)) {
                            argument = emit_api_argument(record->type, record->meta,
                                                         expression.operands[index]->type,
                                                         std::move(argument));
                        }
                    }
                }
                call += argument;
            }
            if (zero_arity_vararg)
                call += "godot::String()";
            call += ")";
            if (expression.type.kind != TypeKind::void_type) {
                result += "return " +
                          (is_intrinsic ? call : emit_api_return(expression.type, std::move(call)));
            } else {
                result += call;
            }
            return result + "; }())";
        }
        if (callee.resolution == ir::ResolutionKind::script_free)
            return "memdelete(" + emit_expression(*callee.operands.at(0)) + ")";
        if (godot_method && callee.value == "get_script" && expression.operands.size() == 1) {
            std::string object = "this";
            if (callee.kind == ir::ExpressionKind::member) {
                object = emit_expression(*callee.operands.at(0));
                bool ref_counted = api_.inherits(callee.operands.at(0)->type.name, "RefCounted") ||
                                   !inner_cpp_type(callee.operands.at(0)->type.name).empty();
                if (!ref_counted && script_symbols_) {
                    if (const auto* symbol =
                            script_symbols_->find_global(callee.operands.at(0)->type.name)) {
                        ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
                    }
                }
                if (ref_counted)
                    object = "(" + object + ").ptr()";
            }
            return "gdpp::runtime::script_identity(" + object + ")";
        }
        // A local declared/inherited signal is already owned by this native object. Constructing
        // a temporary godot::Signal for every `pulse.emit(...)` iteration performs a redundant
        // owner/name lookup and is measurably expensive on Windows. Emit through Object directly,
        // cache the immutable StringName per call site, and still materialize arguments in source
        // order before entering Godot. Keep arbitrary `other.signal.emit()` on the general Signal
        // path because a null external receiver must retain Godot's ordinary error semantics.
        if (callee.kind == ir::ExpressionKind::member && callee.value == "emit" &&
            !callee.operands.empty()) {
            const auto& signal = *callee.operands.at(0);
            const bool local_signal =
                signal.resolution == ir::ResolutionKind::script_signal &&
                (signal.kind == ir::ExpressionKind::identifier ||
                 (signal.kind == ir::ExpressionKind::member && !signal.operands.empty() &&
                  signal.operands.at(0)->kind == ir::ExpressionKind::identifier &&
                  signal.operands.at(0)->value == "self"));
            if (local_signal) {
                const auto suffix = std::to_string(temporary_counter_++);
                const auto signal_name = "_gdpp_signal_name_" + suffix;
                std::string result = "([&]() { static const godot::StringName " + signal_name +
                                     " = " + godot_string_name(signal.value) + "; ";
                for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                    result += "const auto _gdpp_signal_argument_" + suffix + "_" +
                              std::to_string(index - 1) + " = " +
                              emit_expression(*expression.operands[index]) + "; ";
                }
                result += "this->emit_signal(" + signal_name;
                for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                    result += ", _gdpp_signal_argument_" + suffix + "_" + std::to_string(index - 1);
                }
                return result + "); }())";
            }
        }
        if (callee.kind == ir::ExpressionKind::member && callee.value == "call" &&
            callee.operands.at(0)->type.kind == TypeKind::builtin &&
            callee.operands.at(0)->type.name == "Callable") {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto callable_name = "_gdpp_callable_" + suffix;
            std::string result = "([&]() -> godot::Variant { auto &&" + callable_name + " = " +
                                 emit_expression(*callee.operands.at(0)) + "; ";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                result += "const auto _gdpp_callable_argument_" + suffix + "_" +
                          std::to_string(index - 1) + " = " +
                          emit_expression(*expression.operands[index]) + "; ";
            }
            result += "return " + callable_name + ".call(";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                if (index > 1)
                    result += ", ";
                result += "_gdpp_callable_argument_" + suffix + "_" + std::to_string(index - 1);
            }
            return result + "); }())";
        }
        if (callee.resolution == ir::ResolutionKind::dynamic_method) {
            const auto identity = temporary_counter_++;
            const auto suffix = std::to_string(identity);
            const auto target_name = "_gdpp_dynamic_target_" + suffix;
            const auto method_name = "_gdpp_dynamic_method_" + suffix;
            std::string result = "([&]() -> godot::Variant { godot::Variant " + target_name +
                                 " = " +
                                 (callee.kind == ir::ExpressionKind::identifier
                                      ? std::string{"this"}
                                      : emit_expression(*callee.operands.at(0))) +
                                 "; static const godot::StringName " + method_name + " = " +
                                 godot_string_name(callee.value) + "; ";
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                result += "const godot::Variant _gdpp_dynamic_argument_" + suffix + "_" +
                          std::to_string(index - 1) + " = " +
                          emit_expression(*expression.operands[index]) + "; ";
            }
            result += "return gdpp::runtime::call_dynamic(" + target_name + ", " + method_name;
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                result += ", _gdpp_dynamic_argument_" + suffix + "_" + std::to_string(index - 1);
            }
            const auto invocation = result + "); }())";
            return expression.coroutine_call || expression.type.is_dynamic()
                       ? invocation
                       : emit_conversion(expression.type, {TypeKind::variant, "Variant"},
                                         invocation);
        }
        if (!in_function_body_) {
            std::string direct =
                callee.resolution == ir::ResolutionKind::script_constructor
                    ? detail_namespace_ + "::ScriptResource<" + callee.resolved_owner +
                          ">{}.instantiate"
                : callee.resolution == ir::ResolutionKind::inner_constructor
                    ? detail_namespace_ + "::InternalClassResource<" +
                          inner_cpp_type(callee.resolved_owner) + ">{}.instantiate"
                : callee.resolution == ir::ResolutionKind::script_super && !callee.setter.empty()
                    ? native_super_owner(callee.resolved_owner) + "::" + script_native_name
                    : emit_expression(callee);
            if (callee.resolution == ir::ResolutionKind::godot_method &&
                callee.value == "get_node") {
                direct += "<godot::Node>";
            }
            direct += '(';
            for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                if (index > 1)
                    direct += ", ";
                direct += emit_call_argument(index, emit_expression(*expression.operands[index]));
            }
            append_required_variant_defaults(direct, expression.operands.size() - 1);
            const auto call = direct + ')';
            return godot_method ? emit_api_return(expression.type, call) : call;
        }
        std::string invocation;
        std::string receiver_setup;
        const auto suffix = std::to_string(temporary_counter_++);
        if (callee.resolution == ir::ResolutionKind::script_constructor) {
            invocation =
                detail_namespace_ + "::ScriptResource<" + callee.resolved_owner + ">{}.instantiate";
        } else if (callee.resolution == ir::ResolutionKind::inner_constructor) {
            invocation = detail_namespace_ + "::InternalClassResource<" +
                         inner_cpp_type(callee.resolved_owner) + ">{}.instantiate";
        } else if (callee.resolution == ir::ResolutionKind::script_super &&
                   !callee.setter.empty()) {
            invocation = native_super_owner(callee.resolved_owner) + "::" + script_native_name;
        } else if (callee.kind == ir::ExpressionKind::member &&
                   callee.resolution != ir::ResolutionKind::script_super &&
                   callee.operands.at(0)->resolution != ir::ResolutionKind::godot_type &&
                   callee.operands.at(0)->resolution != ir::ResolutionKind::script_type &&
                   callee.operands.at(0)->resolution != ir::ResolutionKind::inner_type) {
            const auto receiver = "_gdpp_call_receiver_" + suffix;
            receiver_setup =
                "auto &&" + receiver + " = " + emit_expression(*callee.operands.at(0)) + "; ";
            const auto connector =
                callee.operands.at(0)->type.kind == TypeKind::object ? "->" : ".";
            invocation = receiver + connector + script_native_name;
            if (callee.resolution == ir::ResolutionKind::godot_method &&
                callee.value == "get_node") {
                invocation += "<godot::Node>";
            }
        } else {
            invocation = emit_expression(callee);
            if (callee.resolution == ir::ResolutionKind::godot_method &&
                callee.value == "get_node") {
                invocation += "<godot::Node>";
            }
        }
        if (expression.operands.size() == 1 && receiver_setup.empty()) {
            const auto call = invocation + "()";
            return godot_method ? emit_api_return(expression.type, call) : call;
        }
        std::string result = "([&]()";
        if (expression.type.kind != TypeKind::void_type)
            result += " -> " + (expression.coroutine_call ? std::string{"godot::Variant"}
                                                          : cpp_type(expression.type));
        result += " { " + receiver_setup;
        for (std::size_t index = 1; index < expression.operands.size(); ++index) {
            const auto& argument = *expression.operands[index];
            const auto temporary =
                "_gdpp_call_argument_" + suffix + "_" + std::to_string(index - 1);
            const auto native_type = cpp_type(argument.type);
            if (argument.kind == ir::ExpressionKind::identifier && argument.value == "self" &&
                native_type.rfind("godot::Ref<", 0) == 0) {
                result += "const " + native_type + " " + temporary + " = this; ";
            } else {
                result += "const auto " + temporary + " = " + emit_expression(argument) + "; ";
            }
        }
        std::string call = invocation + "(";
        for (std::size_t index = 1; index < expression.operands.size(); ++index) {
            if (index > 1)
                call += ", ";
            call += emit_call_argument(index, "_gdpp_call_argument_" + suffix + "_" +
                                                  std::to_string(index - 1));
        }
        append_required_variant_defaults(call, expression.operands.size() - 1);
        call += ")";
        if (expression.type.kind != TypeKind::void_type) {
            result += "return " +
                      (godot_method ? emit_api_return(expression.type, std::move(call)) : call);
        } else {
            result += call;
        }
        return result + "; }())";
    }
    case ir::ExpressionKind::member: {
        if (expression.resolution == ir::ResolutionKind::inner_type)
            return inner_cpp_type(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::external_callable) {
            return "gdpp::runtime::external_callable(" +
                   emit_expression(*expression.operands.at(0)) + ", " +
                   godot_string_name(expression.value) + ")";
        }
        if (expression.resolution == ir::ResolutionKind::external_signal) {
            return "gdpp::runtime::external_signal(" + emit_expression(*expression.operands.at(0)) +
                   ", " + godot_string_name(expression.value) + ")";
        }
        if (expression.resolution == ir::ResolutionKind::script_enum_type)
            return sanitize_qualified_identifier(expression.resolved_owner);
        if (expression.resolution == ir::ResolutionKind::script_constant &&
            !expression.resolved_owner.empty()) {
            const auto inner_owner = inner_cpp_type(expression.resolved_owner);
            return (inner_owner.empty() ? expression.resolved_owner : inner_owner) +
                   "::" + sanitize_identifier(expression.value) +
                   (managed_static_constant(expression.type) ? "()" : "");
        }
        if (expression.resolution == ir::ResolutionKind::script_super)
            return native_super_owner(expression.resolved_owner) +
                   "::" + sanitize_identifier(expression.value);
        if (expression.resolution == ir::ResolutionKind::script_signal) {
            auto object = emit_expression(*expression.operands.at(0));
            bool ref_counted = api_.inherits(expression.operands.at(0)->type.name, "RefCounted") ||
                               !inner_cpp_type(expression.operands.at(0)->type.name).empty();
            if (!ref_counted && script_symbols_) {
                if (const auto* symbol =
                        script_symbols_->find_global(expression.operands.at(0)->type.name)) {
                    ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
                }
            }
            if (ref_counted && object != "this")
                object = "(" + object + ").ptr()";
            return "godot::Signal(" + object + ", " + godot_string_name(expression.value) + ")";
        }
        if (expression.resolution == ir::ResolutionKind::script_callable) {
            auto object = emit_expression(*expression.operands.at(0));
            bool ref_counted = api_.inherits(expression.operands.at(0)->type.name, "RefCounted") ||
                               !inner_cpp_type(expression.operands.at(0)->type.name).empty();
            if (!ref_counted && script_symbols_) {
                if (const auto* symbol =
                        script_symbols_->find_global(expression.operands.at(0)->type.name)) {
                    ref_counted = api_.inherits(symbol->godot_base_type, "RefCounted");
                }
            }
            if (ref_counted && object != "this")
                object = "(" + object + ").ptr()";
            return "godot::Callable(" + object + ", " + godot_string_name(expression.value) + ")";
        }
        if (expression.resolution == ir::ResolutionKind::global_constant ||
            expression.resolution == ir::ResolutionKind::global_enum_value)
            return expression.resolved_owner;
        if (expression.resolution == ir::ResolutionKind::builtin_constant)
            return builtin_constant_expression(expression.resolved_owner);
        const auto object = expression.operands.at(0)->resolution == ir::ResolutionKind::script_type
                                ? expression.operands.at(0)->resolved_owner
                                : emit_expression(*expression.operands.at(0));
        if (expression.resolution == ir::ResolutionKind::dynamic_property) {
            if (expression.operands.at(0)->type.kind == TypeKind::dictionary) {
                const auto key =
                    "_gdpp_dictionary_read_key_" + std::to_string(temporary_counter_++);
                return "(" + object +
                       ").get(([]() -> const godot::Variant& { static const godot::Variant " + key +
                       " = " + godot_string_name(expression.value) + "; return " + key +
                       "; }()), godot::Variant())";
            }
            return "gdpp::runtime::get_named(" + object + ", " +
                   godot_string_name(expression.value) + ")";
        }
        const auto connector =
            expression.resolution == ir::ResolutionKind::enum_member ||
                    expression.resolution == ir::ResolutionKind::builtin_constant
                ? "::"
            : expression.operands.at(0)->resolution == ir::ResolutionKind::godot_type      ? "::"
            : expression.operands.at(0)->resolution == ir::ResolutionKind::script_type     ? "::"
            : expression.operands.at(0)->resolution == ir::ResolutionKind::inner_type      ? "::"
            : object == "this" || expression.operands.at(0)->type.kind == TypeKind::object ? "->"
                                                                                           : ".";
        if (expression.resolution == ir::ResolutionKind::godot_property &&
            !expression.direct_access && !expression.getter.empty()) {
            std::string index;
            if (expression.indexed_argument >= 0) {
                index = std::to_string(expression.indexed_argument);
                if (const auto* getter =
                        api_.find_method(expression.resolved_owner, expression.getter)) {
                    if (const auto* argument = api_.argument(*getter, 0))
                        index = emit_api_argument(argument->type, argument->meta,
                                                  {TypeKind::integer, "int"}, std::move(index));
                }
            }
            return emit_api_return(expression.type,
                                   object + connector + expression.getter + "(" + index + ")");
        }
        if (expression.resolution == ir::ResolutionKind::script_property &&
            !expression.getter.empty()) {
            return object + connector + expression.getter + "()";
        }
        if (expression.resolution == ir::ResolutionKind::godot_property &&
            expression.direct_access && expression.operands.at(0)->type.kind == TypeKind::builtin) {
            return emit_direct_builtin_member(expression.resolved_owner, object, expression.value);
        }
        return object + connector +
               (expression.resolution == ir::ResolutionKind::enum_member
                    ? enum_identifier(expression.value)
                    : sanitize_identifier(expression.value)) +
               (expression.resolution == ir::ResolutionKind::script_constant &&
                        managed_static_constant(expression.type)
                    ? "()"
                    : "");
    }
    case ir::ExpressionKind::subscript:
        if (expression.operands.at(0)->type.is_dynamic() ||
            expression.operands.at(0)->type.kind == TypeKind::object) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto target_name = "_gdpp_dynamic_target_" + suffix;
            const auto key_name = "_gdpp_dynamic_key_" + suffix;
            return "([&]() -> godot::Variant { godot::Variant " + target_name + " = " +
                   emit_expression(*expression.operands.at(0)) + "; const godot::Variant " +
                   key_name + " = " + emit_expression(*expression.operands.at(1)) +
                   "; return gdpp::runtime::get_key(" + target_name + ", " + key_name + "); }())";
        }
        return emit_subscript_read(expression.operands.at(0)->type, expression.type,
                                   emit_expression(*expression.operands.at(0)) + "[" +
                                       emit_expression(*expression.operands.at(1)) + "]");
    case ir::ExpressionKind::array_literal: {
        if (expression.operands.empty())
            return "godot::Array()";
        const auto suffix = std::to_string(temporary_counter_++);
        const auto array = "_gdpp_array_" + suffix;
        const auto capture = in_function_body_ ? "[&]" : "[]";
        std::string result = std::string{"("} + capture + "() -> godot::Array { godot::Array " +
                             array + "; " + array + ".resize(" +
                             std::to_string(expression.operands.size()) + "); ";
        for (std::size_t index = 0; index < expression.operands.size(); ++index) {
            const auto value = "_gdpp_array_value_" + suffix + "_" + std::to_string(index);
            result += "{ const godot::Variant " + value + " = " +
                      emit_expression(*expression.operands[index]) + "; " + array + "[" +
                      std::to_string(index) + "] = " + value + "; } ";
        }
        return result + "return " + array + "; }())";
    }
    case ir::ExpressionKind::dictionary_literal: {
        const auto suffix = std::to_string(temporary_counter_++);
        const auto dictionary = "_gdpp_dictionary_" + suffix;
        const auto capture = in_function_body_ ? "[&]" : "[]";
        std::string result = std::string{"("} + capture +
                             "() -> godot::Dictionary { godot::Dictionary " + dictionary + "; ";
        for (std::size_t index = 0; index + 1 < expression.operands.size(); index += 2) {
            const auto entry = suffix + "_" + std::to_string(index / 2);
            const auto key = "_gdpp_dictionary_key_" + entry;
            const auto value = "_gdpp_dictionary_value_" + entry;
            result += "{ const godot::Variant " + key + " = " +
                      emit_expression(*expression.operands[index]) + "; const godot::Variant " +
                      value + " = " + emit_expression(*expression.operands[index + 1]) + "; " +
                      dictionary + ".set(" + key + ", " + value + "); } ";
        }
        return result + "return " + dictionary + "; }())";
    }
    case ir::ExpressionKind::lambda: {
        if (!expression.lambda) {
            diagnostics_.error("GDS3007", "lambda expression is missing typed IR payload",
                               expression.span);
            return "godot::Callable()";
        }
        const auto& lambda = *expression.lambda;
        const auto required =
            std::count_if(lambda.parameters.begin(), lambda.parameters.end(),
                          [](const auto& parameter) { return !parameter.default_value; });
        const auto identity = temporary_counter_++;
        const auto arguments = "_gdpp_lambda_arguments_" + std::to_string(identity);
        std::string result = "gdpp::runtime::make_local_callable(";
        result += lambda.owner_bound ? "this" : "nullptr";
        result += ", " + std::to_string(required) + ", " +
                  std::to_string(lambda.parameters.size()) + ", [=](const auto &" + arguments +
                  ") mutable -> godot::Variant {\n";
        for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
            const auto& parameter = lambda.parameters[index];
            result += "    " + cpp_type(parameter.type) + " " +
                      sanitize_identifier(parameter.name) + " = ";
            const auto converted = emit_conversion(parameter.type, {TypeKind::variant, "Variant"},
                                                   arguments + "[" + std::to_string(index) + "]");
            if (parameter.default_value) {
                result += arguments + ".size() > " + std::to_string(index) + " ? " + converted +
                          " : " + emit_expression(*parameter.default_value);
            } else {
                result += converted;
            }
            result += ";\n";
        }
        const auto saved_return = current_return_type_;
        const auto saved_callable = in_callable_lambda_;
        const auto saved_function = in_function_body_;
        current_return_type_ = lambda.return_type;
        in_callable_lambda_ = true;
        in_function_body_ = true;
        result += emit_statements(lambda.body, 1, 0, parameter_locals(lambda.parameters));
        current_return_type_ = saved_return;
        in_callable_lambda_ = saved_callable;
        in_function_body_ = saved_function;
        // Typed non-void lambdas have already passed semantic all-path return
        // analysis. Adding a defensive return here creates unreachable C++ and
        // fails warning-clean MSVC builds. A void GDScript lambda still needs a
        // Variant result for Callable's native bridge.
        if (lambda.return_type.kind == TypeKind::void_type ||
            requires_native_fallback(lambda.body) ||
            (lambda.return_type.is_dynamic() && native_statements_fall_through(lambda.body)))
            result += "    return godot::Variant();\n";
        result += "})";
        return result;
    }
    }
    return "godot::Variant()";
}

std::string CodeGenerator::emit_statements(
    const std::vector<ir::Statement>& statements, const std::size_t indentation,
    const std::size_t begin, const std::vector<std::pair<std::string, Type>>& entry_locals) const {
    const auto saved_types = current_local_types_;
    const auto saved_ambiguous = ambiguous_local_names_;
    const auto saved_overrides = local_expression_overrides_;
    for (const auto& [name, type] : entry_locals) {
        if (const auto [_, inserted] = current_local_types_.emplace(name, type); !inserted)
            ambiguous_local_names_.insert(name);
    }
    collect_local_declarations(statements, current_local_types_, ambiguous_local_names_);
    auto result = emit_async_statements(statements, indentation, begin, {}, {}, false);
    current_local_types_ = saved_types;
    ambiguous_local_names_ = saved_ambiguous;
    local_expression_overrides_ = saved_overrides;
    return result;
}

std::vector<std::pair<std::string, Type>>
CodeGenerator::parameter_locals(const std::vector<ir::Parameter>& parameters) {
    std::vector<std::pair<std::string, Type>> result;
    result.reserve(parameters.size());
    for (const auto& parameter : parameters)
        result.emplace_back(parameter.name, parameter.type);
    return result;
}

std::string CodeGenerator::lift_async_loop_locals(const ir::Statement& statement,
                                                  const std::size_t indentation) const {
    std::unordered_set<std::string> assigned;
    std::unordered_set<std::string> declared;
    collect_assigned_names(statement.body, assigned);
    collect_declared_names(statement.body, declared);
    std::set<std::string> ordered(assigned.begin(), assigned.end());
    std::string result;
    for (const auto& name : ordered) {
        if (declared.find(name) != declared.end() ||
            local_expression_overrides_.find(name) != local_expression_overrides_.end()) {
            continue;
        }
        const auto type = current_local_types_.find(name);
        if (type == current_local_types_.end())
            continue;
        if (ambiguous_local_names_.find(name) != ambiguous_local_names_.end()) {
            diagnostics_.error("GDS3006",
                               "async loop-carried local '" + name +
                                   "' is shadowed and requires symbol-identity frame lowering",
                               statement.span);
            continue;
        }
        const auto cell = "_gdpp_async_cell_" + std::to_string(temporary_counter_++);
        result += indent(indentation) + "const auto " + cell + " = std::make_shared<" +
                  cpp_type(type->second) + ">(" + sanitize_identifier(name) + ");\n";
        local_expression_overrides_[name] = "(*" + cell + ")";
    }
    return result;
}

bool CodeGenerator::statement_contains_await(const ir::Statement& statement) const noexcept {
    if (statement.kind == ir::StatementKind::await_statement ||
        statement.kind == ir::StatementKind::await_variable)
        return await_can_suspend(statement);
    const auto contains = [this](const std::vector<ir::Statement>& statements) {
        return std::any_of(statements.begin(), statements.end(),
                           [this](const auto& child) { return statement_contains_await(child); });
    };
    return contains(statement.body) || contains(statement.else_body) ||
           contains(statement.guard_prefix) || contains(statement.assert_condition_prefix) ||
           contains(statement.assert_message_prefix);
}

bool CodeGenerator::await_can_suspend(const ir::Statement& statement) noexcept {
    if (!statement.expression)
        return false;
    const auto& type = statement.expression->type;
    return statement.expression->coroutine_call || type.is_dynamic() ||
           (type.kind == TypeKind::builtin && type.name == "Signal");
}

std::string CodeGenerator::async_return(const std::size_t indentation,
                                        const bool continuation_context) const {
    if (continuation_context)
        return indent(indentation) + "return;\n";
    if (current_coroutine_abi_)
        return indent(indentation) + "return gdpp::runtime::coroutine_result(" +
               current_coroutine_state_ + ");\n";
    if (current_return_type_.kind == TypeKind::void_type)
        return indent(indentation) + "return;\n";
    return indent(indentation) + "return {};\n";
}

std::string CodeGenerator::coroutine_return(const std::size_t indentation, std::string value,
                                            const bool continuation_context) const {
    const auto prefix = indent(indentation);
    std::string result = prefix + "gdpp::runtime::complete_coroutine(" + current_coroutine_state_ +
                         ", godot::Variant(" + std::move(value) + "));\n";
    if (continuation_context)
        return result + prefix + "return;\n";
    return result + prefix + "return gdpp::runtime::coroutine_result(" + current_coroutine_state_ +
           ");\n";
}

bool CodeGenerator::can_emit_flat_async(const ir::Function& function,
                                        const mir::Function& mir_function) const noexcept {
    // A long source-level callback chain produces recursively nested lambda types. MSVC can use
    // more than 10 GiB compiling such a function before failing with C1060. Flat MIR dispatch is
    // safe whenever the function has no locals that must be lifted across suspension and no loop
    // or match state that still requires a dedicated coroutine frame. Shorter/complex functions
    // keep the structured lowering until general local liveness is represented in MIR.
    const auto suspensions = std::count_if(
        mir_function.blocks.begin(), mir_function.blocks.end(),
        [](const auto& block) { return block.terminator.kind == mir::TerminatorKind::suspend; });
    return mir_function.suspends && suspensions >= 8 &&
           std::all_of(function.body.begin(), function.body.end(), flat_async_statement_supported);
}

std::string CodeGenerator::emit_flat_async(const mir::Function& function,
                                           const std::size_t indentation) const {
    const auto prefix = indent(indentation);
    const auto identity = std::to_string(temporary_counter_++);
    const auto step_type = "_gdpp_async_step_type_" + identity;
    const auto step = "_gdpp_async_step_" + identity;
    const auto weak_step = "_gdpp_async_weak_step_" + identity;
    const auto keep_alive = "_gdpp_async_keep_alive_" + identity;
    const auto pc = "_gdpp_async_pc_" + identity;
    const auto values = "_gdpp_async_values_" + identity;

    std::string result;
    result += prefix + "using " + step_type +
              " = std::function<void(std::size_t, const godot::Array &)>;\n";
    result += prefix + "const auto " + step + " = std::make_shared<" + step_type + ">();\n";
    result += prefix + "const std::weak_ptr<" + step_type + "> " + weak_step + " = " + step + ";\n";
    result += prefix + "*" + step + " = [=](std::size_t " + pc + ", const godot::Array &" + values +
              ") mutable {\n";
    result += indent(indentation + 1) + "static_cast<void>(" + values + ");\n";
    result +=
        indent(indentation + 1) + "const auto " + keep_alive + " = " + weak_step + ".lock();\n";
    result += indent(indentation + 1) + "if (!" + keep_alive + ") return;\n";
    result += indent(indentation + 1) + "while (true) {\n";
    result += indent(indentation + 2) + "switch (" + pc + ") {\n";

    const auto saved_async_continuation = in_async_continuation_;
    in_async_continuation_ = true;
    for (const auto& block : function.blocks) {
        result += indent(indentation + 2) + "case " + std::to_string(block.id) + ": {\n";
        for (const auto& instruction : block.instructions) {
            if (!instruction.source || instruction.kind == mir::InstructionKind::suspend_value)
                continue;
            result += emit_statement(*instruction.source, indentation + 3);
        }
        const auto& terminator = block.terminator;
        switch (terminator.kind) {
        case mir::TerminatorKind::jump:
            result += indent(indentation + 3) + pc + " = " +
                      std::to_string(terminator.targets.front()) + ";\n";
            result += indent(indentation + 3) + "continue;\n";
            break;
        case mir::TerminatorKind::branch:
            result += indent(indentation + 3) + pc + " = (" + emit_truthy(*terminator.condition) +
                      ") ? " + std::to_string(terminator.targets[0]) + " : " +
                      std::to_string(terminator.targets[1]) + ";\n";
            result += indent(indentation + 3) + "continue;\n";
            break;
        case mir::TerminatorKind::suspend: {
            const auto target = std::to_string(terminator.targets.front());
            const auto awaitable = "_gdpp_flat_awaitable_" + std::to_string(temporary_counter_++);
            result += indent(indentation + 3) + "const godot::Variant " + awaitable + "(" +
                      emit_expression(*terminator.condition) + ");\n";
            result += indent(indentation + 3) + "if (" + awaitable +
                      ".get_type() != godot::Variant::SIGNAL) {\n";
            result += indent(indentation + 4) + pc + " = " + target + ";\n";
            result += indent(indentation + 4) + "continue;\n";
            result += indent(indentation + 3) + "}\n";
            result += indent(indentation + 3) + "if (!gdpp::runtime::await_signal(" + awaitable +
                      ", this, [" + keep_alive + "](const godot::Array &resume_values) { (*" +
                      keep_alive + ")(" + target + ", resume_values); })) {\n";
            result += current_coroutine_abi_
                          ? coroutine_return(indentation + 4, "godot::Variant{}", true)
                          : indent(indentation + 4) + "return;\n";
            result += indent(indentation + 3) + "}\n";
            result += indent(indentation + 3) + "return;\n";
            break;
        }
        case mir::TerminatorKind::return_value:
            if (current_coroutine_abi_) {
                result +=
                    coroutine_return(indentation + 3,
                                     terminator.condition ? emit_expression(*terminator.condition)
                                                          : std::string{"godot::Variant{}"},
                                     true);
            } else {
                result += indent(indentation + 3) + "return;\n";
            }
            break;
        case mir::TerminatorKind::stop:
            result += current_coroutine_abi_
                          ? coroutine_return(indentation + 3, "godot::Variant{}", true)
                          : indent(indentation + 3) + "return;\n";
            break;
        case mir::TerminatorKind::invalid:
            result += indent(indentation + 3) + "return;\n";
            break;
        }
        result += indent(indentation + 2) + "}\n";
    }
    in_async_continuation_ = saved_async_continuation;
    result += indent(indentation + 2) + "default: return;\n";
    result += indent(indentation + 2) + "}\n";
    result += indent(indentation + 1) + "}\n";
    result += prefix + "};\n";
    result += prefix + "(*" + step + ")(" + std::to_string(function.entry) + ", godot::Array{});\n";
    if (current_coroutine_abi_)
        result += async_return(indentation, false);
    return result;
}

std::string CodeGenerator::emit_async_match_branch(
    const ir::Statement& branch, const std::size_t next_branch, const std::size_t after_branch,
    const std::string& value_name, const std::string& keep_alive, const std::size_t indentation,
    std::shared_ptr<const AsyncLoopControl> loop_control) const {
    std::string condition;
    std::vector<MatchBinding> bindings;
    for (const auto& pattern : branch.patterns) {
        collect_match_bindings(pattern, bindings);
        if (!condition.empty())
            condition += " || ";
        condition += "(" + emit_match_pattern(pattern, value_name, bindings) + ")";
    }
    if (condition.empty())
        condition = "false";

    const auto prefix = indent(indentation);
    std::string result;
    for (const auto& binding : bindings)
        result += prefix + "godot::Variant " + binding.slot + ";\n";
    result += prefix + "if (" + condition + ") {\n";
    const auto content_indent = indentation + 1;
    for (const auto& binding : bindings) {
        result += indent(content_indent) + cpp_type(binding.type) + " " +
                  sanitize_identifier(binding.name) + " = " +
                  emit_conversion(binding.type, {TypeKind::variant, "Variant"}, binding.slot) +
                  ";\n";
    }

    const auto dispatch = [&](const std::size_t target, const std::size_t target_indent) {
        return indent(target_indent) + "(*" + keep_alive + ")(" + std::to_string(target) + ");\n";
    };
    const auto emit_branch_body = [&](const std::size_t body_indent) -> std::string {
        return emit_async_statements(branch.body, body_indent, 0, {}, dispatch(after_branch, 0),
                                     true, loop_control);
    };
    const auto emit_fallback = [&](const std::size_t fallback_indent) -> std::string {
        return dispatch(next_branch, fallback_indent);
    };

    if (!branch.expression) {
        result += emit_branch_body(content_indent);
    } else if (branch.guard_prefix.empty()) {
        result += indent(content_indent) + "if (" + emit_truthy(*branch.expression) + ") {\n";
        result += emit_branch_body(content_indent + 1);
        result += indent(content_indent) + "} else {\n";
        result += emit_fallback(content_indent + 1);
        result += indent(content_indent) + "}\n";
    } else {
        std::string completion = "if (" + emit_truthy(*branch.expression) + ") {\n";
        completion += emit_branch_body(1);
        completion += "} else {\n";
        completion += emit_fallback(1);
        completion += "}\n";
        result += emit_async_statements(branch.guard_prefix, content_indent, 0, {}, completion,
                                        true, loop_control);
    }
    result += prefix + "} else {\n";
    result += emit_fallback(indentation + 1);
    result += prefix + "}\n";
    return result;
}

std::string CodeGenerator::emit_assert_failure(const ir::Statement& statement,
                                               const std::size_t indentation,
                                               const bool continuation_context) const {
    const auto location = current_source_path_ + ":" + std::to_string(statement.span.begin.line);
    std::string message = godot_string("Assertion failed at " + location);
    if (statement.expression) {
        message += " + godot::String(\": \") + godot::String(" +
                   emit_expression(*statement.expression) + ")";
    }
    const auto prefix = indent(indentation);
    if (current_coroutine_abi_) {
        auto result = prefix + "gdpp::runtime::complete_coroutine(" + current_coroutine_state_ +
                      ", godot::Variant{});\n";
        if (continuation_context)
            return result + prefix + "ERR_FAIL_EDMSG(" + message + ");\n";
        return result + prefix + "ERR_FAIL_V_EDMSG(gdpp::runtime::coroutine_result(" +
               current_coroutine_state_ + "), " + message + ");\n";
    }
    if (continuation_context || current_return_type_.kind == TypeKind::void_type)
        return prefix + "ERR_FAIL_EDMSG(" + message + ");\n";
    return prefix + "ERR_FAIL_V_EDMSG({}, " + message + ");\n";
}

std::string CodeGenerator::emit_async_statements(
    const std::vector<ir::Statement>& statements, const std::size_t indentation,
    const std::size_t begin, std::vector<StatementSlice> tails, const std::string& terminal,
    const bool continuation_context, std::shared_ptr<const AsyncLoopControl> loop_control) const {
    std::string result;
    for (std::size_t index = begin; index < statements.size(); ++index) {
        const auto& statement = statements[index];
        if (loop_control && statement.kind == ir::StatementKind::break_statement) {
            result += emit_async_statements({}, indentation, 0, loop_control->break_tails,
                                            loop_control->break_terminal, continuation_context,
                                            loop_control->parent);
            result += async_return(indentation, continuation_context);
            return result;
        }
        if (loop_control && statement.kind == ir::StatementKind::continue_statement) {
            result += indent(indentation) + loop_control->continue_terminal + "\n";
            result += async_return(indentation, continuation_context);
            return result;
        }
        const bool requires_loop_control = loop_control && contains_current_loop_control(statement);
        if (!statement_contains_await(statement) && !requires_loop_control) {
            if (statement.kind == ir::StatementKind::return_statement) {
                if (current_coroutine_abi_ || !continuation_context) {
                    const auto saved_async_continuation = in_async_continuation_;
                    in_async_continuation_ = in_async_continuation_ || continuation_context;
                    result += emit_statement(statement, indentation);
                    in_async_continuation_ = saved_async_continuation;
                } else {
                    result += async_return(indentation, true);
                }
                return result;
            }
            {
                const auto saved_async_continuation = in_async_continuation_;
                in_async_continuation_ = in_async_continuation_ || continuation_context;
                result += emit_statement(statement, indentation);
                in_async_continuation_ = saved_async_continuation;
            }
            continue;
        }

        auto continuation = tails;
        if (index + 1 < statements.size())
            continuation.insert(continuation.begin(), StatementSlice{&statements, index + 1});
        const auto prefix = indent(indentation);
        if (statement.kind == ir::StatementKind::await_statement ||
            statement.kind == ir::StatementKind::await_variable) {
            const auto identity = std::to_string(temporary_counter_++);
            const auto awaitable_name = "_gdpp_awaitable_" + identity;
            const auto result_name = "_gdpp_await_values_" + identity;
            const auto resume_name = "_gdpp_resume_" + identity;
            const auto immediate_name = "_gdpp_immediate_" + identity;
            result += prefix + "const godot::Variant " + awaitable_name + "(" +
                      emit_expression(*statement.expression) + ");\n";
            result += prefix + "auto " + resume_name + " = [=](const godot::Array &" + result_name +
                      ") mutable {\n";
            if (statement.kind == ir::StatementKind::await_variable) {
                result += indent(indentation + 1) + cpp_type(statement.declared_type) + " " +
                          sanitize_identifier(statement.name) + " = " +
                          emit_conversion(statement.declared_type, {TypeKind::variant, "Variant"},
                                          "gdpp::runtime::await_result(" + result_name + ")") +
                          ";\n";
            }
            result += emit_async_statements(statements, indentation + 1, index + 1, tails, terminal,
                                            true, loop_control);
            result += prefix + "};\n";
            result +=
                prefix + "if (" + awaitable_name + ".get_type() != godot::Variant::SIGNAL) {\n";
            result += indent(indentation + 1) + "godot::Array " + immediate_name + ";\n";
            result +=
                indent(indentation + 1) + immediate_name + ".push_back(" + awaitable_name + ");\n";
            result += indent(indentation + 1) + resume_name + "(" + immediate_name + ");\n";
            result += async_return(indentation + 1, continuation_context);
            result += prefix + "}\n";
            result += prefix + "if (!gdpp::runtime::await_signal(" + awaitable_name + ", this, " +
                      resume_name + ")) {\n";
            result += current_coroutine_abi_ ? coroutine_return(indentation + 1, "godot::Variant{}",
                                                                continuation_context)
                                             : async_return(indentation + 1, continuation_context);
            result += prefix + "}\n" + async_return(indentation, continuation_context);
            return result;
        }

        if (statement.kind == ir::StatementKind::if_statement) {
            result += prefix + "if (" + emit_truthy(*statement.condition) + ") {\n";
            result += emit_async_statements(statement.body, indentation + 1, 0, continuation,
                                            terminal, continuation_context, loop_control);
            result += prefix + "} else {\n";
            result += emit_async_statements(statement.else_body, indentation + 1, 0, continuation,
                                            terminal, continuation_context, loop_control);
            result += prefix + "}\n";
            return result;
        }

        if (statement.kind == ir::StatementKind::assert_statement) {
            const auto identity = std::to_string(temporary_counter_++);
            const auto after_assert = "_gdpp_after_assert_" + identity;
            result += prefix + "{\n" + indent(indentation + 1) + "auto " + after_assert +
                      " = [=]() mutable {\n";
            result += emit_async_statements({}, indentation + 2, 0, continuation, terminal, true,
                                            loop_control);
            result += indent(indentation + 1) + "};\n";

            const auto prefix_suspends = [this](const std::vector<ir::Statement>& statements) {
                return std::any_of(statements.begin(), statements.end(), [this](const auto& child) {
                    return statement_contains_await(child);
                });
            };
            const auto condition_continuation =
                continuation_context || prefix_suspends(statement.assert_condition_prefix);
            const auto failure = emit_assert_failure(statement, 0, true);
            const auto message_path =
                emit_async_statements(statement.assert_message_prefix, 1, 0, {}, failure,
                                      condition_continuation, loop_control);
            std::string condition_terminal = "if (" + emit_truthy(*statement.condition) + ") {\n";
            condition_terminal += indent(1) + after_assert + "();\n";
            condition_terminal += "} else {\n" + message_path + "}\n";

            result += indent(indentation + 1) + "#ifdef DEBUG_ENABLED\n";
            result +=
                emit_async_statements(statement.assert_condition_prefix, indentation + 1, 0, {},
                                      condition_terminal, continuation_context, loop_control);
            result += indent(indentation + 1) + "#else\n" + indent(indentation + 1) + after_assert +
                      "();\n" + indent(indentation + 1) + "#endif\n";
            result += async_return(indentation + 1, continuation_context) + prefix + "}\n";
            return result;
        }

        if (statement.kind == ir::StatementKind::match_statement) {
            const auto identity = std::to_string(match_counter_++);
            const auto value_name = "_gdpp_async_match_value_" + identity;
            const auto dispatch_type = "_gdpp_async_match_dispatch_type_" + identity;
            const auto dispatch = "_gdpp_async_match_dispatch_" + identity;
            const auto weak_dispatch = "_gdpp_async_match_weak_dispatch_" + identity;
            const auto keep_alive = "_gdpp_async_match_keep_alive_" + identity;
            const auto branch_index = "_gdpp_async_match_branch_" + identity;
            const auto after_branch = statement.body.size();
            result += prefix + "{\n" + indent(indentation + 1) + "const auto " + value_name +
                      " = " + emit_expression(*statement.condition) + ";\n" +
                      indent(indentation + 1) + "using " + dispatch_type +
                      " = std::function<void(std::size_t)>;\n" + indent(indentation + 1) +
                      "const auto " + dispatch + " = std::make_shared<" + dispatch_type + ">();\n" +
                      indent(indentation + 1) + "const std::weak_ptr<" + dispatch_type + "> " +
                      weak_dispatch + " = " + dispatch + ";\n" + indent(indentation + 1) + "*" +
                      dispatch + " = [=](std::size_t " + branch_index + ") mutable {\n" +
                      indent(indentation + 2) + "const auto " + keep_alive + " = " + weak_dispatch +
                      ".lock();\n" + indent(indentation + 2) + "if (!" + keep_alive +
                      ") return;\n" + indent(indentation + 2) + "switch (" + branch_index + ") {\n";
            for (std::size_t branch = 0; branch < statement.body.size(); ++branch) {
                result += indent(indentation + 2) + "case " + std::to_string(branch) + ": {\n";
                result +=
                    emit_async_match_branch(statement.body[branch], branch + 1, after_branch,
                                            value_name, keep_alive, indentation + 3, loop_control);
                result += indent(indentation + 3) + "return;\n" + indent(indentation + 2) + "}\n";
            }
            result += indent(indentation + 2) + "case " + std::to_string(after_branch) + ": {\n";
            result += emit_async_statements({}, indentation + 3, 0, continuation, terminal, true,
                                            loop_control);
            result += indent(indentation + 3) + "return;\n" + indent(indentation + 2) + "}\n" +
                      indent(indentation + 2) + "default: return;\n" + indent(indentation + 2) +
                      "}\n" + indent(indentation + 1) + "};\n" + indent(indentation + 1) + "(*" +
                      dispatch + ")(0);\n" + async_return(indentation + 1, continuation_context) +
                      prefix + "}\n";
            return result;
        }

        if (statement.kind == ir::StatementKind::for_statement) {
            const auto saved_overrides = local_expression_overrides_;
            const auto identity = temporary_counter_++;
            const auto suffix = std::to_string(identity);
            const auto iterable = "_gdpp_async_iterable_" + suffix;
            const auto iterator = "_gdpp_async_iterator_" + suffix;
            const auto available = "_gdpp_async_available_" + suffix;
            const auto loop = "_gdpp_async_loop_" + suffix;
            const auto weak_loop = "_gdpp_async_weak_loop_" + suffix;
            const auto keep_alive = "_gdpp_async_keep_loop_" + suffix;
            result += prefix + "{\n" + lift_async_loop_locals(statement, indentation + 1) +
                      indent(indentation + 1) + "const auto " + iterable +
                      " = std::make_shared<godot::Variant>(" +
                      emit_expression(*statement.condition) + ");\n" + indent(indentation + 1) +
                      "const auto " + iterator + " = std::make_shared<godot::Variant>();\n" +
                      indent(indentation + 1) + "const auto " + available +
                      " = std::make_shared<bool>(gdpp::runtime::iter_init(*" + iterable + ", *" +
                      iterator + "));\n" + indent(indentation + 1) + "const auto " + loop +
                      " = std::make_shared<std::function<void()>>();\n" + indent(indentation + 1) +
                      "const std::weak_ptr<std::function<void()>> " + weak_loop + " = " + loop +
                      ";\n" + indent(indentation + 1) + "*" + loop + " = [=]() mutable {\n" +
                      indent(indentation + 2) + "const auto " + keep_alive + " = " + weak_loop +
                      ".lock();\n" + indent(indentation + 2) + "if (!" + keep_alive +
                      ") return;\n" + indent(indentation + 2) + "if (!*" + available + ") {\n";
            result += emit_async_statements({}, indentation + 3, 0, continuation, terminal, true,
                                            loop_control);
            result +=
                async_return(indentation + 3, true) + indent(indentation + 2) + "}\n" +
                indent(indentation + 2) +
                (statement.declared_type.is_dynamic() ? std::string{"godot::Variant"}
                                                      : cpp_type(statement.declared_type)) +
                " " + sanitize_identifier(statement.name) + " = " +
                emit_conversion(statement.declared_type, {TypeKind::variant, "Variant"},
                                "gdpp::runtime::iter_get(*" + iterable + ", *" + iterator + ")") +
                ";\n";
            const auto advance = "*" + available + " = gdpp::runtime::iter_next(*" + iterable +
                                 ", *" + iterator + "); (*" + keep_alive + ")();";
            auto nested_loop = std::make_shared<AsyncLoopControl>();
            nested_loop->break_tails = continuation;
            nested_loop->break_terminal = terminal;
            nested_loop->continue_terminal = advance;
            nested_loop->parent = loop_control;
            result += emit_async_statements(statement.body, indentation + 2, 0, {},
                                            advance + " return;", true, nested_loop);
            result += indent(indentation + 1) + "};\n" + indent(indentation + 1) + "(*" + loop +
                      ")();\n" + async_return(indentation + 1, continuation_context) + prefix +
                      "}\n";
            local_expression_overrides_ = saved_overrides;
            return result;
        }

        if (statement.kind == ir::StatementKind::while_statement) {
            const auto saved_overrides = local_expression_overrides_;
            const auto suffix = std::to_string(temporary_counter_++);
            const auto loop = "_gdpp_async_loop_" + suffix;
            const auto weak_loop = "_gdpp_async_weak_loop_" + suffix;
            const auto keep_alive = "_gdpp_async_keep_loop_" + suffix;
            result += prefix + "{\n" + lift_async_loop_locals(statement, indentation + 1) +
                      indent(indentation + 1) + "const auto " + loop +
                      " = std::make_shared<std::function<void()>>();\n" + indent(indentation + 1) +
                      "const std::weak_ptr<std::function<void()>> " + weak_loop + " = " + loop +
                      ";\n" + indent(indentation + 1) + "*" + loop + " = [=]() mutable {\n" +
                      indent(indentation + 2) + "const auto " + keep_alive + " = " + weak_loop +
                      ".lock();\n" + indent(indentation + 2) + "if (!" + keep_alive +
                      ") return;\n" + indent(indentation + 2) + "if (!(" +
                      emit_truthy(*statement.condition) + ")) {\n";
            result += emit_async_statements({}, indentation + 3, 0, continuation, terminal, true,
                                            loop_control);
            result += async_return(indentation + 3, true) + indent(indentation + 2) + "}\n";
            auto nested_loop = std::make_shared<AsyncLoopControl>();
            nested_loop->break_tails = continuation;
            nested_loop->break_terminal = terminal;
            nested_loop->continue_terminal = "(*" + keep_alive + ")();";
            nested_loop->parent = loop_control;
            result += emit_async_statements(statement.body, indentation + 2, 0, {},
                                            "(*" + keep_alive + ")(); return;", true, nested_loop);
            result += indent(indentation + 1) + "};\n" + indent(indentation + 1) + "(*" + loop +
                      ")();\n" + async_return(indentation + 1, continuation_context) + prefix +
                      "}\n";
            local_expression_overrides_ = saved_overrides;
            return result;
        }

        diagnostics_.error("GDS3006", "unsupported structured await reached code generation",
                           statement.span);
        return result;
    }

    if (!tails.empty()) {
        const auto next = tails.front();
        tails.erase(tails.begin());
        if (next.statements) {
            result +=
                emit_async_statements(*next.statements, indentation, next.begin, std::move(tails),
                                      terminal, continuation_context, std::move(loop_control));
        }
    } else if (!terminal.empty()) {
        result += indent_block(indentation, terminal);
    } else if (current_coroutine_abi_) {
        result += coroutine_return(indentation, "godot::Variant{}", continuation_context);
    }
    return result;
}

void CodeGenerator::collect_match_bindings(const ir::MatchPattern& pattern,
                                           std::vector<MatchBinding>& bindings) const {
    if (pattern.kind == ir::MatchPatternKind::binding) {
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
            return binding.name == pattern.name;
        });
        if (found == bindings.end()) {
            bindings.push_back({pattern.name,
                                "_gdpp_match_bind_" + std::to_string(temporary_counter_++),
                                pattern.type});
        }
    }
    for (const auto& element : pattern.elements)
        collect_match_bindings(element, bindings);
}

std::string CodeGenerator::emit_match_pattern(const ir::MatchPattern& pattern,
                                              const std::string& candidate,
                                              const std::vector<MatchBinding>& bindings) const {
    switch (pattern.kind) {
    case ir::MatchPatternKind::value: {
        const auto suffix = std::to_string(temporary_counter_++);
        const auto actual = "_gdpp_pattern_actual_" + suffix;
        const auto expected = "_gdpp_pattern_expected_" + suffix;
        return "([&]() -> bool { const godot::Variant " + actual + " = godot::Variant(" +
               candidate + "); const godot::Variant " + expected + " = godot::Variant(" +
               emit_expression(*pattern.expression) +
               "); return static_cast<bool>(gdpp::runtime::binary(" + "godot::Variant::OP_EQUAL, " +
               actual + ", " + expected + ")); }())";
    }
    case ir::MatchPatternKind::wildcard:
    case ir::MatchPatternKind::rest:
        return "true";
    case ir::MatchPatternKind::binding: {
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
            return binding.name == pattern.name;
        });
        if (found == bindings.end()) {
            diagnostics_.error("GDS3005", "match binding has no native storage slot", pattern.span);
            return "false";
        }
        return "([&]() -> bool { " + found->slot + " = godot::Variant(" + candidate +
               "); return true; }())";
    }
    case ir::MatchPatternKind::array: {
        const auto suffix = std::to_string(temporary_counter_++);
        const auto actual = "_gdpp_pattern_array_value_" + suffix;
        const auto array = "_gdpp_pattern_array_" + suffix;
        const bool has_rest =
            !pattern.elements.empty() && pattern.elements.back().kind == ir::MatchPatternKind::rest;
        const auto fixed_size = pattern.elements.size() - (has_rest ? 1U : 0U);
        std::string result =
            "([&]() -> bool { const godot::Variant " + actual + " = godot::Variant(" + candidate +
            "); if (" + actual + ".get_type() != godot::Variant::ARRAY) return false; " +
            "const godot::Array " + array + " = " + actual + "; if (" + array + ".size() " +
            (has_rest ? "< " : "!= ") + std::to_string(fixed_size) + ") return false; ";
        for (std::size_t index = 0; index < fixed_size; ++index) {
            result += "if (!(" +
                      emit_match_pattern(pattern.elements[index],
                                         array + "[" + std::to_string(index) + "]", bindings) +
                      ")) return false; ";
        }
        return result + "return true; }())";
    }
    case ir::MatchPatternKind::dictionary: {
        const auto suffix = std::to_string(temporary_counter_++);
        const auto actual = "_gdpp_pattern_dictionary_value_" + suffix;
        const auto dictionary = "_gdpp_pattern_dictionary_" + suffix;
        const bool has_rest = !pattern.elements.empty() && !pattern.keys.empty() &&
                              !pattern.keys.back() &&
                              pattern.elements.back().kind == ir::MatchPatternKind::rest;
        const auto fixed_size = pattern.elements.size() - (has_rest ? 1U : 0U);
        std::string result = "([&]() -> bool { const godot::Variant " + actual +
                             " = godot::Variant(" + candidate + "); if (" + actual +
                             ".get_type() != godot::Variant::DICTIONARY) return false; " +
                             "const godot::Dictionary " + dictionary + " = " + actual + "; if (" +
                             dictionary + ".size() " + (has_rest ? "< " : "!= ") +
                             std::to_string(fixed_size) + ") return false; ";
        for (std::size_t index = 0; index < fixed_size; ++index) {
            const auto key = "_gdpp_pattern_key_" + std::to_string(temporary_counter_++);
            result += "const godot::Variant " + key + " = godot::Variant(" +
                      emit_expression(*pattern.keys[index]) + "); if (!" + dictionary + ".has(" +
                      key + ")) return false; ";
            if (pattern.elements[index].kind != ir::MatchPatternKind::wildcard) {
                result += "if (!(" +
                          emit_match_pattern(pattern.elements[index], dictionary + "[" + key + "]",
                                             bindings) +
                          ")) return false; ";
            }
        }
        return result + "return true; }())";
    }
    }
    diagnostics_.error("GDS3005", "unknown match pattern reached code generation", pattern.span);
    return "false";
}

std::string CodeGenerator::emit_statement(const ir::Statement& statement,
                                          std::size_t indentation) const {
    const auto prefix = indent(indentation);
    switch (statement.kind) {
    case ir::StatementKind::expression:
        // GDScript permits intentionally ignoring any expression result. Make that intent
        // explicit so nodiscard Godot value types and ordinary conversion expressions remain
        // warning-clean under commercial -Wall/-Wextra builds.
        return prefix + "static_cast<void>(" + emit_expression(*statement.expression) + ");\n";
    case ir::StatementKind::return_statement:
        if (in_callable_lambda_) {
            return prefix + "return " +
                   (statement.expression
                        ? "godot::Variant(" +
                              emit_conversion(current_return_type_, statement.expression->type,
                                              emit_expression(*statement.expression)) +
                              ")"
                        : "godot::Variant()") +
                   ";\n";
        }
        if (current_coroutine_abi_) {
            const auto value =
                statement.expression
                    ? emit_conversion(current_return_type_, statement.expression->type,
                                      emit_expression(*statement.expression))
                    : std::string{"godot::Variant{}"};
            return coroutine_return(indentation, value, in_async_continuation_);
        }
        if (in_async_continuation_)
            return prefix + "return;\n";
        if (!statement.expression && current_return_type_.kind != TypeKind::void_type)
            return prefix + "return {};\n";
        return prefix + "return" +
               (statement.expression
                    ? " " + emit_conversion(current_return_type_, statement.expression->type,
                                            emit_expression(*statement.expression))
                    : "") +
               ";\n";
    case ir::StatementKind::await_statement:
        if (await_can_suspend(statement)) {
            diagnostics_.error("GDS3006", "nested await reached code generation", statement.span);
            return prefix + "/* invalid nested await */;\n";
        }
        return prefix + "static_cast<void>(" + emit_expression(*statement.expression) + ");\n";
    case ir::StatementKind::await_variable:
        if (await_can_suspend(statement)) {
            diagnostics_.error("GDS3006", "nested await reached code generation", statement.span);
            return prefix + "/* invalid nested await */;\n";
        }
        return prefix + (statement.is_constant ? "const " : "") +
               cpp_type(statement.declared_type) + " " + sanitize_identifier(statement.name) +
               " = " +
               emit_conversion(statement.declared_type, statement.expression->type,
                               emit_expression(*statement.expression)) +
               ";\n";
    case ir::StatementKind::assert_statement: {
        std::string result = prefix + "#ifdef DEBUG_ENABLED\n";
        for (const auto& child : statement.assert_condition_prefix)
            result += emit_statement(child, indentation);
        result += prefix + "if (!(" + emit_truthy(*statement.condition) + ")) {\n";
        for (const auto& child : statement.assert_message_prefix)
            result += emit_statement(child, indentation + 1);
        result += emit_assert_failure(statement, indentation + 1, in_async_continuation_);
        result += prefix + "}\n";
        return result + prefix + "#endif\n";
    }
    case ir::StatementKind::variable:
        return prefix + (statement.is_constant ? "const " : "") +
               (statement.expression && statement.expression->kind == ir::ExpressionKind::lambda
                    ? std::string{"auto"}
                    : cpp_type(statement.declared_type)) +
               " " + sanitize_identifier(statement.name) +
               (statement.expression
                    ? " = " + emit_conversion(statement.declared_type, statement.expression->type,
                                              emit_expression(*statement.expression))
                    : "{}") +
               ";\n";
    case ir::StatementKind::assignment: {
        const auto& target = *statement.condition;
        if (target.resolution == ir::ResolutionKind::dynamic_property &&
            target.kind == ir::ExpressionKind::member && !target.operands.empty() &&
            target.operands.at(0)->type.kind == TypeKind::dictionary) {
            return emit_dictionary_member_assignment(statement, indentation);
        }
        if (target.resolution == ir::ResolutionKind::dynamic_property)
            return emit_dynamic_assignment(statement, indentation);
        if (target.kind == ir::ExpressionKind::subscript &&
            (target.operands.at(0)->type.is_dynamic() ||
             target.operands.at(0)->type.kind == TypeKind::object)) {
            return emit_dynamic_assignment(statement, indentation);
        }
        if (target.kind == ir::ExpressionKind::subscript) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto container_name = "_gdpp_subscript_container_" + suffix;
            const auto index_name = "_gdpp_subscript_index_" + suffix;
            const auto value_name = "_gdpp_subscript_value_" + suffix;
            const auto nested_prefix = indent(indentation + 1);
            std::string result = prefix + "{\n" + nested_prefix + "auto &&" + container_name +
                                 " = " + emit_expression(*target.operands.at(0)) + ";\n" +
                                 nested_prefix + "const auto " + index_name + " = " +
                                 emit_expression(*target.operands.at(1)) + ";\n";
            std::string assigned;
            if (statement.operation != "=") {
                const auto current_name = "_gdpp_subscript_current_" + suffix;
                result += nested_prefix + "const auto " + current_name + " = " +
                          emit_subscript_read(target.operands.at(0)->type, target.type,
                                              container_name + "[" + index_name + "]") +
                          ";\n";
                result += nested_prefix + "const auto " + value_name + " = " +
                          emit_expression(*statement.expression) + ";\n";
                const auto operation =
                    statement.operation.substr(0, statement.operation.size() - 1);
                if (target.type.is_dynamic() || statement.expression->type.is_dynamic()) {
                    assigned =
                        "gdpp::runtime::binary(godot::Variant::" + variant_operator(operation) +
                        ", " + current_name + ", " + value_name + ")";
                } else {
                    assigned = "(" + current_name + " " + operation + " " + value_name + ")";
                }
            } else {
                result += nested_prefix + "const auto " + value_name + " = " +
                          emit_expression(*statement.expression) + ";\n";
                assigned = value_name;
            }
            const auto assigned_source_type =
                statement.operation == "=" ? statement.expression->type
                : target.type.is_dynamic() || statement.expression->type.is_dynamic()
                    ? Type{TypeKind::variant, "Variant"}
                    : target.type;
            assigned = emit_conversion(target.type, assigned_source_type, std::move(assigned));
            assigned = emit_subscript_store(target.operands.at(0)->type, std::move(assigned));
            result += nested_prefix + container_name + "[" + index_name + "] = " + assigned +
                      ";\n" + prefix + "}\n";
            return result;
        }
        std::string value = emit_expression(*statement.expression);
        if (statement.operation != "=") {
            const auto operation = statement.operation.substr(0, statement.operation.size() - 1);
            if (!target.type.is_dynamic() && target.type.is_numeric() &&
                statement.expression->type.is_dynamic()) {
                value = emit_conversion(target.type, statement.expression->type, std::move(value));
                value = "(" + emit_expression(*statement.condition) + " " + operation + " " +
                        value + ")";
            } else if (statement.condition->type.is_dynamic() ||
                       statement.expression->type.is_dynamic()) {
                value = "gdpp::runtime::binary(godot::Variant::" + variant_operator(operation) +
                        ", " + emit_expression(*statement.condition) + ", " + value + ")";
            } else {
                value = "(" + emit_expression(*statement.condition) + " " + operation + " " +
                        value + ")";
            }
        }
        const auto assigned_source_type =
            statement.operation == "=" ? statement.expression->type
            : target.type.is_dynamic() || statement.expression->type.is_dynamic()
                ? Type{TypeKind::variant, "Variant"}
                : target.type;
        value = emit_conversion(target.type, assigned_source_type, std::move(value));
        if (target.resolution == ir::ResolutionKind::godot_property && target.direct_access &&
            target.kind == ir::ExpressionKind::member) {
            std::vector<const ir::Expression*> direct_chain;
            const ir::Expression* root = &target;
            while (root->resolution == ir::ResolutionKind::godot_property && root->direct_access &&
                   root->kind == ir::ExpressionKind::member && !root->operands.empty()) {
                direct_chain.push_back(root);
                root = root->operands.front().get();
            }
            if (root->resolution == ir::ResolutionKind::godot_property && !root->direct_access &&
                !root->getter.empty() && !root->setter.empty()) {
                const auto suffix = std::to_string(temporary_counter_++);
                const auto root_name = "_gdpp_property_value_" + suffix;
                const auto nested_prefix = indent(indentation + 1);
                std::string result = prefix + "{\n";
                std::string receiver;
                std::string connector;
                if (root->kind == ir::ExpressionKind::member) {
                    const auto receiver_name = "_gdpp_property_receiver_" + suffix;
                    result += nested_prefix + "auto &&" + receiver_name + " = " +
                              emit_expression(*root->operands.at(0)) + ";\n";
                    receiver = receiver_name;
                    connector = root->operands.at(0)->type.kind == TypeKind::object ? "->" : ".";
                }
                result += nested_prefix + cpp_type(root->type) + " " + root_name + " = " +
                          receiver + connector + root->getter + "(";
                std::string index;
                const auto* getter = api_.find_method(root->resolved_owner, root->getter);
                if (root->indexed_argument >= 0) {
                    index = std::to_string(root->indexed_argument);
                    if (getter) {
                        if (const auto* argument = api_.argument(*getter, 0))
                            index = emit_api_argument(argument->type, argument->meta,
                                                      {TypeKind::integer, "int"}, std::move(index));
                    }
                }
                auto assignment_object = root_name;
                for (std::size_t chain_index = direct_chain.size(); chain_index > 1;
                     --chain_index) {
                    const auto* member = direct_chain[chain_index - 1];
                    assignment_object = emit_direct_builtin_member(
                        member->resolved_owner, std::move(assignment_object), member->value);
                }
                result += index + ");\n" + nested_prefix +
                          emit_direct_builtin_assignment(direct_chain.front()->resolved_owner,
                                                         std::move(assignment_object),
                                                         direct_chain.front()->value, value) +
                          ";\n";
                const auto* setter = api_.find_method(root->resolved_owner, root->setter);
                std::string setter_index;
                if (root->indexed_argument >= 0) {
                    setter_index = std::to_string(root->indexed_argument);
                    if (setter) {
                        if (const auto* argument = api_.argument(*setter, 0))
                            setter_index = emit_api_argument(argument->type, argument->meta,
                                                             {TypeKind::integer, "int"},
                                                             std::move(setter_index));
                    }
                    setter_index += ", ";
                }
                auto root_value = root_name;
                if (setter) {
                    const auto value_index = root->indexed_argument >= 0 ? 1U : 0U;
                    if (const auto* argument = api_.argument(*setter, value_index)) {
                        root_value = emit_api_argument(argument->type, argument->meta, root->type,
                                                       std::move(root_value));
                    }
                }
                result += nested_prefix + receiver + connector + root->setter + "(" + setter_index +
                          root_value + ");\n" + prefix + "}\n";
                return result;
            }
            const auto& parent = *target.operands.at(0);
            if (parent.type.kind == TypeKind::builtin) {
                return prefix +
                       emit_direct_builtin_assignment(target.resolved_owner,
                                                      emit_expression(parent), target.value,
                                                      std::move(value)) +
                       ";\n";
            }
        }
        if (target.resolution == ir::ResolutionKind::script_property) {
            if (target.kind == ir::ExpressionKind::member) {
                const auto& owner = *target.operands.at(0);
                if (owner.resolution == ir::ResolutionKind::script_type) {
                    return prefix + owner.resolved_owner + "::" + target.setter + "(" + value +
                           ");\n";
                }
                const auto object = emit_expression(owner);
                const auto connector = owner.type.kind == TypeKind::object ? "->" : ".";
                return prefix + object + connector + target.setter + "(" + value + ");\n";
            }
            return prefix + target.setter + "(" + value + ");\n";
        }
        if (target.resolution == ir::ResolutionKind::godot_property && !target.direct_access) {
            if (target.setter.empty()) {
                diagnostics_.error("GDS3003", "Godot property is read-only", target.span);
                return prefix + "/* read-only property assignment */;\n";
            }
            if (target.kind == ir::ExpressionKind::identifier) {
                std::string index;
                const auto* setter = api_.find_method(target.resolved_owner, target.setter);
                if (target.indexed_argument >= 0) {
                    index = std::to_string(target.indexed_argument);
                    if (setter) {
                        if (const auto* argument = api_.argument(*setter, 0))
                            index = emit_api_argument(argument->type, argument->meta,
                                                      {TypeKind::integer, "int"}, std::move(index));
                    }
                    index += ", ";
                }
                if (setter) {
                    const auto value_index = target.indexed_argument >= 0 ? 1U : 0U;
                    if (const auto* argument = api_.argument(*setter, value_index)) {
                        value = emit_api_argument(argument->type, argument->meta, target.type,
                                                  std::move(value));
                    }
                }
                return prefix + target.setter + "(" + index + value + ");\n";
            }
            if (target.kind == ir::ExpressionKind::member) {
                const auto object = emit_expression(*target.operands.at(0));
                const auto connector =
                    target.operands.at(0)->resolution == ir::ResolutionKind::script_type ? "::"
                    : object == "this" || target.operands.at(0)->type.kind == TypeKind::object
                        ? "->"
                        : ".";
                std::string index;
                const auto* setter = api_.find_method(target.resolved_owner, target.setter);
                if (target.indexed_argument >= 0) {
                    index = std::to_string(target.indexed_argument);
                    if (setter) {
                        if (const auto* argument = api_.argument(*setter, 0))
                            index = emit_api_argument(argument->type, argument->meta,
                                                      {TypeKind::integer, "int"}, std::move(index));
                    }
                    index += ", ";
                }
                if (setter) {
                    const auto value_index = target.indexed_argument >= 0 ? 1U : 0U;
                    if (const auto* argument = api_.argument(*setter, value_index)) {
                        value = emit_api_argument(argument->type, argument->meta, target.type,
                                                  std::move(value));
                    }
                }
                return prefix + object + connector + target.setter + "(" + index + value + ");\n";
            }
        }
        return prefix + emit_expression(target) + " = " + value + ";\n";
    }
    case ir::StatementKind::if_statement: {
        std::string result = prefix + "if (" + emit_truthy(*statement.condition) + ") {\n";
        for (const auto& child : statement.body)
            result += emit_statement(child, indentation + 1);
        result += prefix + "}";
        if (!statement.else_body.empty()) {
            result += " else {\n";
            for (const auto& child : statement.else_body) {
                result += emit_statement(child, indentation + 1);
            }
            result += prefix + "}";
        }
        return result + "\n";
    }
    case ir::StatementKind::match_statement: {
        const auto identity = match_counter_++;
        const auto value_name = "_gdpp_match_value_" + std::to_string(identity);
        const auto matched_name = "_gdpp_match_done_" + std::to_string(identity);
        std::string result = prefix + "{\n" + indent(indentation + 1) + "const auto " + value_name +
                             " = " + emit_expression(*statement.condition) + ";\n" +
                             indent(indentation + 1) + "bool " + matched_name + " = false;\n";
        for (const auto& branch : statement.body) {
            std::string condition;
            std::vector<MatchBinding> bindings;
            for (const auto& pattern : branch.patterns) {
                collect_match_bindings(pattern, bindings);
                if (!condition.empty())
                    condition += " || ";
                condition += "(" + emit_match_pattern(pattern, value_name, bindings) + ")";
            }
            const auto branch_prefix = indent(indentation + 1);
            for (const auto& binding : bindings)
                result += branch_prefix + "godot::Variant " + binding.slot + ";\n";
            result += branch_prefix + "if (!" + matched_name;
            if (!condition.empty())
                result += " && (" + condition + ")";
            result += ") {\n";
            const auto content_indent = indentation + 2;
            for (const auto& binding : bindings) {
                result +=
                    indent(content_indent) + cpp_type(binding.type) + " " +
                    sanitize_identifier(binding.name) + " = " +
                    emit_conversion(binding.type, {TypeKind::variant, "Variant"}, binding.slot) +
                    ";\n";
            }
            for (const auto& guard_statement : branch.guard_prefix)
                result += emit_statement(guard_statement, content_indent);
            if (branch.expression) {
                const auto guard = emit_truthy(*branch.expression);
                const auto guard_condition =
                    guard.size() >= 2 && guard.front() == '(' && guard.back() == ')'
                        ? guard
                        : "(" + guard + ")";
                result += indent(content_indent) + "if " + guard_condition + " {\n" +
                          indent(content_indent + 1) + matched_name + " = true;\n";
                for (const auto& child : branch.body)
                    result += emit_statement(child, content_indent + 1);
                result += indent(content_indent) + "}\n";
            } else {
                result += indent(content_indent) + matched_name + " = true;\n";
                for (const auto& child : branch.body)
                    result += emit_statement(child, content_indent);
            }
            result += branch_prefix + "}\n";
        }
        return result + prefix + "}\n";
    }
    case ir::StatementKind::match_branch:
        diagnostics_.error("GDS3004", "orphan match branch reached code generation",
                           statement.span);
        return prefix + "/* invalid match branch */;\n";
    case ir::StatementKind::while_statement: {
        std::string result = prefix + "while (" + emit_truthy(*statement.condition) + ") {\n";
        for (const auto& child : statement.body)
            result += emit_statement(child, indentation + 1);
        return result + prefix + "}\n";
    }
    case ir::StatementKind::for_statement: {
        if (const auto* direct_range = range_call(*statement.condition)) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto start = "_gdpp_range_start_" + suffix;
            const auto stop = "_gdpp_range_stop_" + suffix;
            const auto step = "_gdpp_range_step_" + suffix;
            const auto value = "_gdpp_range_value_" + suffix;
            const auto argument_count = direct_range->operands.size() - 1;
            const auto integer_argument = [&](const std::size_t index) {
                const auto& argument = *direct_range->operands.at(index);
                return emit_conversion({TypeKind::integer, "int"}, argument.type,
                                       emit_expression(argument));
            };
            const auto start_value = argument_count == 1 ? std::string{"0"} : integer_argument(1);
            const auto stop_value = argument_count == 1 ? integer_argument(1) : integer_argument(2);
            const auto step_value = argument_count == 3 ? integer_argument(3) : std::string{"1"};
            const auto nested_prefix = indent(indentation + 1);
            const auto body_prefix = indent(indentation + 2);
            std::string result =
                prefix + "{\n" + nested_prefix + "const int64_t " + start + " = " + start_value +
                ";\n" + nested_prefix + "const int64_t " + stop + " = " + stop_value + ";\n" +
                nested_prefix + "const int64_t " + step + " = " + step_value + ";\n" +
                nested_prefix + "if (" + step +
                " == 0) godot::UtilityFunctions::push_error(\"GDPP: range step cannot be "
                "zero\");\n" +
                nested_prefix + "for (int64_t " + value + " = " + start + "; " + step +
                " != 0 && (" + step + " > 0 ? " + value + " < " + stop + " : " + value + " > " +
                stop + "); " + value + " += " + step + ") {\n" + body_prefix +
                (statement.declared_type.is_dynamic() ? std::string{"godot::Variant"}
                                                      : cpp_type(statement.declared_type)) +
                " " + sanitize_identifier(statement.name) + " = " +
                emit_conversion(statement.declared_type, {TypeKind::integer, "int"}, value) + ";\n";
            for (const auto& child : statement.body)
                result += emit_statement(child, indentation + 2);
            return result + nested_prefix + "}\n" + prefix + "}\n";
        }
        if (statement.condition->type.kind == TypeKind::integer) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto limit = "_gdpp_integer_limit_" + suffix;
            std::string result =
                prefix + "{\n" + indent(indentation + 1) + "const int64_t " + limit + " = " +
                emit_expression(*statement.condition) + ";\n" + indent(indentation + 1) + "for (" +
                (statement.declared_type.is_dynamic() ? std::string{"int64_t"}
                                                      : cpp_type(statement.declared_type)) +
                " " + sanitize_identifier(statement.name) + " = 0; " +
                sanitize_identifier(statement.name) + " < " + limit + "; ++" +
                sanitize_identifier(statement.name) + ") {\n";
            for (const auto& child : statement.body)
                result += emit_statement(child, indentation + 2);
            return result + indent(indentation + 1) + "}\n" + prefix + "}\n";
        }
        if (statement.condition->type.is_packed_array()) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto iterable_name = "_gdpp_packed_iterable_" + suffix;
            const auto index_name = "_gdpp_packed_index_" + suffix;
            const auto size_name = "_gdpp_packed_size_" + suffix;
            const auto nested_prefix = indent(indentation + 1);
            const auto body_prefix = indent(indentation + 2);
            const auto element_type = packed_array_element_type(statement.condition->type);
            std::string result =
                prefix + "{\n" + nested_prefix + "const auto " + iterable_name + " = " +
                emit_expression(*statement.condition) + ";\n" + nested_prefix + "const int64_t " +
                size_name + " = " + iterable_name + ".size();\n" + nested_prefix + "for (int64_t " +
                index_name + " = 0; " + index_name + " < " + size_name + "; ++" + index_name +
                ") {\n" + body_prefix +
                (statement.declared_type.is_dynamic() ? std::string{"godot::Variant"}
                                                      : cpp_type(statement.declared_type)) +
                " " + sanitize_identifier(statement.name) + " = " +
                emit_conversion(statement.declared_type, element_type,
                                iterable_name + "[" + index_name + "]") +
                ";\n";
            for (const auto& child : statement.body)
                result += emit_statement(child, indentation + 2);
            return result + nested_prefix + "}\n" + prefix + "}\n";
        }
        if (statement.condition->type.is_dynamic() ||
            statement.condition->type.kind == TypeKind::array ||
            statement.condition->type.kind == TypeKind::dictionary ||
            statement.condition->type.kind == TypeKind::string) {
            const auto suffix = std::to_string(temporary_counter_++);
            const auto iterable_name = "_gdpp_dynamic_iterable_" + suffix;
            const auto iterator_name = "_gdpp_dynamic_iterator_" + suffix;
            const auto available_name = "_gdpp_dynamic_available_" + suffix;
            const auto nested_prefix = indent(indentation + 1);
            const auto body_prefix = indent(indentation + 2);
            std::string result =
                prefix + "{\n" + nested_prefix + "const godot::Variant " + iterable_name + " = " +
                emit_expression(*statement.condition) + ";\n" + nested_prefix + "godot::Variant " +
                iterator_name + ";\n" + nested_prefix + "for (bool " + available_name +
                " = gdpp::runtime::iter_init(" + iterable_name + ", " + iterator_name + "); " +
                available_name + "; " + available_name + " = gdpp::runtime::iter_next(" +
                iterable_name + ", " + iterator_name + ")) {\n" + body_prefix +
                (statement.declared_type.is_dynamic() ? std::string{"godot::Variant"}
                                                      : cpp_type(statement.declared_type)) +
                " " + sanitize_identifier(statement.name) + " = " +
                emit_conversion(statement.declared_type, {TypeKind::variant, "Variant"},
                                "gdpp::runtime::iter_get(" + iterable_name + ", " + iterator_name +
                                    ")") +
                ";\n";
            for (const auto& child : statement.body)
                result += emit_statement(child, indentation + 2);
            return result + nested_prefix + "}\n" + prefix + "}\n";
        }
        diagnostics_.error("GDS3005",
                           "unsupported statically typed iterable reached code generation",
                           statement.span);
        return prefix + "/* unsupported iterable */;\n";
    }
    case ir::StatementKind::pass_statement:
        return prefix + ";\n";
    case ir::StatementKind::break_statement:
        return prefix + "break;\n";
    case ir::StatementKind::continue_statement:
        return prefix + "continue;\n";
    }
    return {};
}

void CodeGenerator::emit_inner_class_declaration(const ir::Class& declaration,
                                                 std::ostringstream& header,
                                                 const std::string& native_name,
                                                 const std::string& source_name) const {
    const auto* previous_inner_script = current_inner_script_;
    const auto previous_function_parameters = local_function_parameters_;
    const auto previous_functions = local_functions_;
    const auto previous_native_class_name = current_native_class_name_;
    local_function_parameters_.clear();
    local_functions_.clear();
    current_native_class_name_ = native_name;
    for (const auto& function : declaration.functions) {
        local_functions_.emplace(function.name, &function);
        auto& parameters = local_function_parameters_[function.name];
        for (const auto& parameter : function.parameters)
            parameters.push_back(parameter.type);
    }
    current_inner_script_ = script_symbols_ && current_script_
                                ? script_symbols_->find_inner(*current_script_, source_name)
                                : nullptr;
    const auto resolved_base = inner_base_names_.find(source_name);
    const auto source_base =
        resolved_base == inner_base_names_.end() ? declaration.base_type : resolved_base->second;
    const auto native_inner_base = inner_cpp_type(source_base);
    const auto base_cpp =
        native_inner_base.empty() ? "godot::" + declaration.base_type : native_inner_base;
    const auto godot_base = inner_godot_base_type(source_name);
    const auto engine_virtual_for = [&](const ir::Function& function) {
        const auto* method =
            function.is_static ? nullptr : api_.find_method(godot_base, function.name);
        return method && method->is_virtual ? method : nullptr;
    };
    const auto coroutine_abi_for = [&](const ir::Function& function) {
        return function.is_coroutine && !function.is_static && !engine_virtual_for(function);
    };
    const auto function_return_type = [&](const ir::Function& function) {
        return coroutine_abi_for(function) ? std::string{"godot::Variant"}
                                           : cpp_type(function.return_type);
    };
    const auto initializer =
        std::find_if(declaration.functions.begin(), declaration.functions.end(),
                     [](const auto& function) { return function.name == "_init"; });
    const bool has_instance_initializers =
        std::any_of(declaration.fields.begin(), declaration.fields.end(), [](const auto& field) {
            return !field.is_constant && !field.is_static && !field.onready && field.initializer;
        });
    header << "class " << native_name << " : public " << base_cpp << " {\n"
           << "    GDCLASS(" << native_name << ", " << base_cpp << ")\n\n"
           << "public:\n";
    for (const auto& enumeration : declaration.enums) {
        if (enumeration.name.empty()) {
            for (const auto& entry : enumeration.entries)
                header << "    inline static constexpr int64_t " << enum_identifier(entry.name)
                       << " = " << entry.value << ";\n";
        } else {
            header << "    struct " << sanitize_identifier(enumeration.name) << " {\n";
            for (const auto& entry : enumeration.entries)
                header << "        inline static constexpr int64_t " << enum_identifier(entry.name)
                       << " = " << entry.value << ";\n";
            header << "    };\n";
        }
    }
    for (const auto& field : declaration.fields) {
        if (!field.is_constant)
            continue;
        header << "    static const " << cpp_type(field.type);
        if (managed_constant_field(field))
            header << "& " << sanitize_identifier(field.name) << "();\n";
        else
            header << ' ' << sanitize_identifier(field.name) << ";\n";
    }
    if (initializer != declaration.functions.end()) {
        const auto required =
            std::count_if(initializer->parameters.begin(), initializer->parameters.end(),
                          [](const auto& parameter) { return !parameter.default_value; });
        if (required != 0)
            header << "    " << native_name
                   << (has_instance_initializers ? "();\n" : "() = default;\n");
        header << "    " << native_name << '(';
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                header << ", ";
            const auto& parameter = initializer->parameters[index];
            header << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
            if (parameter.default_value)
                header << " = " << emit_parameter_default(parameter);
        }
        header << ");\n";
    } else if (has_instance_initializers) {
        header << "    " << native_name << "();\n";
    }
    header << "\nprotected:\n    static void _bind_methods();\n";
    bool fields = false;
    for (const auto& field : declaration.fields) {
        if (field.is_constant)
            continue;
        fields = true;
        header << "    ";
        if (field.is_static) {
            const auto name = sanitize_identifier(field.name);
            const auto type = cpp_type(field.type);
            header << "static std::atomic<" << type << "*>& _gdpp_static_" << name
                   << "_pointer();\n"
                   << "    static std::mutex& _gdpp_static_" << name << "_mutex();\n"
                   << "    static " << type << "& _gdpp_static_" << name << "_storage();\n"
                   << "    static void _gdpp_static_" << name << "_release();\n";
        } else {
            header << cpp_type(field.type) << ' ' << sanitize_identifier(field.name) << "{};\n";
        }
    }
    for (const auto& field : declaration.fields) {
        if (cached_preload_field(field) && !field.is_static) {
            header << "    static " << cpp_type(field.type) << "& _gdpp_preloaded_"
                   << sanitize_identifier(field.name) << "();\n";
        }
    }
    for (const auto& field : declaration.fields) {
        if (!managed_constant_field(field))
            continue;
        const auto name = sanitize_identifier(field.name);
        header << "    static " << cpp_type(field.type) << "& _gdpp_constant_" << name
               << "_storage();\n"
               << "    static bool& _gdpp_constant_" << name << "_ready();\n"
               << "    static std::mutex& _gdpp_constant_" << name << "_mutex();\n";
    }
    if (!fields)
        header << "    // No internal class fields.\n";
    header << "\npublic:\n"
           << "    static void _gdpp_preload_resources();\n"
           << "    static void _gdpp_release_preloaded_resources();\n";
    for (const auto& field : declaration.fields) {
        if (field.is_constant)
            continue;
        const auto name = sanitize_identifier(field.name);
        const auto setter_parameter = field.setter && field.setter->method.empty()
                                          ? sanitize_identifier(field.setter->parameter)
                                          : "value";
        header << "    " << (field.is_static ? "static " : "") << cpp_type(field.type)
               << " _gdpp_get_" << name << "();\n"
               << "    " << (field.is_static ? "static " : "") << "void _gdpp_set_" << name << '('
               << cpp_type(field.type) << ' ' << setter_parameter << ");\n";
    }
    for (const auto& function : declaration.functions) {
        header << "    " << (function.is_static ? "static " : "virtual ")
               << function_return_type(function) << ' ' << sanitize_identifier(function.name)
               << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0)
                header << ", ";
            const auto& parameter = function.parameters[index];
            header << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
            if (parameter.default_value)
                header << " = " << emit_parameter_default(parameter);
        }
        header << ')';
        if (const auto* method = engine_virtual_for(function); method && method->is_const)
            header << " const";
        if (engine_virtual_for(function) ||
            (!function.is_static && inner_overrides_method(source_base, function.name)))
            header << " override";
        header << ";\n";
        if (const auto* method = engine_virtual_for(function); method && method->is_const) {
            header << "    " << cpp_type(function.return_type) << " _gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    header << ", ";
                const auto& parameter = function.parameters[index];
                header << parameter_native_type(parameter) << ' '
                       << parameter_native_name(parameter);
            }
            header << ");\n";
        }
    }
    header << "};\n\n";
    local_function_parameters_ = previous_function_parameters;
    local_functions_ = previous_functions;
    current_native_class_name_ = previous_native_class_name;
    current_inner_script_ = previous_inner_script;
}

void CodeGenerator::emit_inner_class_definition(const ir::Class& declaration,
                                                std::ostringstream& source,
                                                const std::string& native_name,
                                                const std::string& source_name) const {
    const auto* previous_inner_script = current_inner_script_;
    const auto previous_function_parameters = local_function_parameters_;
    const auto previous_functions = local_functions_;
    const auto previous_native_class_name = current_native_class_name_;
    local_function_parameters_.clear();
    local_functions_.clear();
    current_native_class_name_ = native_name;
    for (const auto& function : declaration.functions) {
        local_functions_.emplace(function.name, &function);
        auto& parameters = local_function_parameters_[function.name];
        for (const auto& parameter : function.parameters)
            parameters.push_back(parameter.type);
    }
    current_inner_script_ = script_symbols_ && current_script_
                                ? script_symbols_->find_inner(*current_script_, source_name)
                                : nullptr;
    const auto godot_base = inner_godot_base_type(source_name);
    const auto engine_virtual_for = [&](const ir::Function& function) {
        const auto* method =
            function.is_static ? nullptr : api_.find_method(godot_base, function.name);
        return method && method->is_virtual ? method : nullptr;
    };
    const auto coroutine_abi_for = [&](const ir::Function& function) {
        return function.is_coroutine && !function.is_static && !engine_virtual_for(function);
    };
    const auto function_return_type = [&](const ir::Function& function) {
        return coroutine_abi_for(function) ? std::string{"godot::Variant"}
                                           : cpp_type(function.return_type);
    };
    const auto initializer =
        std::find_if(declaration.functions.begin(), declaration.functions.end(),
                     [](const auto& function) { return function.name == "_init"; });
    for (const auto& field : declaration.fields) {
        if (!field.is_constant)
            continue;
        const auto name = sanitize_identifier(field.name);
        if (!managed_constant_field(field)) {
            source << "const " << cpp_type(field.type) << ' ' << native_name << "::" << name
                   << " = " << emit_expression(*field.initializer) << ";\n";
            continue;
        }
        const auto type = cpp_type(field.type);
        source << type << "& " << native_name << "::_gdpp_constant_" << name
               << "_storage() {\n    static " << type << " value{};\n    return value;\n}\n\n"
               << "bool& " << native_name << "::_gdpp_constant_" << name
               << "_ready() {\n    static bool value = false;\n    return value;\n}\n\n"
               << "std::mutex& " << native_name << "::_gdpp_constant_" << name
               << "_mutex() {\n    static std::mutex value;\n    return value;\n}\n\n"
               << "const " << type << "& " << native_name << "::" << name << "() {\n"
               << "    std::lock_guard<std::mutex> lock(_gdpp_constant_" << name << "_mutex());\n"
               << "    if (!_gdpp_constant_" << name << "_ready()) {\n"
               << "        _gdpp_constant_" << name << "_storage() = "
               << emit_conversion(field.type, field.initializer->type,
                                  emit_expression(*field.initializer))
               << ";\n"
               << "        _gdpp_constant_" << name << "_ready() = true;\n"
               << "    }\n"
               << "    return _gdpp_constant_" << name << "_storage();\n}\n\n";
    }
    for (const auto& field : declaration.fields) {
        if (!field.is_constant && field.is_static) {
            const auto name = sanitize_identifier(field.name);
            const auto type = cpp_type(field.type);
            source << "std::atomic<" << type << "*>& " << native_name << "::_gdpp_static_" << name
                   << "_pointer() {\n    static std::atomic<" << type
                   << "*> value{nullptr};\n    return value;\n}\n\n"
                   << "std::mutex& " << native_name << "::_gdpp_static_" << name
                   << "_mutex() {\n    static std::mutex value;\n    return value;\n}\n\n"
                   << type << "& " << native_name << "::_gdpp_static_" << name << "_storage() {\n"
                   << "    auto* value = _gdpp_static_" << name
                   << "_pointer().load(std::memory_order_acquire);\n"
                   << "    if (value) return *value;\n"
                   << "    std::lock_guard<std::mutex> lock(_gdpp_static_" << name << "_mutex());\n"
                   << "    value = _gdpp_static_" << name
                   << "_pointer().load(std::memory_order_relaxed);\n"
                   << "    if (!value) {\n        value = new " << type << "(";
            if (field.initializer && !field.onready) {
                source << emit_conversion(field.type, field.initializer->type,
                                          emit_expression(*field.initializer));
            } else {
                source << "{}";
            }
            source << ");\n        _gdpp_static_" << name
                   << "_pointer().store(value, std::memory_order_release);\n    }\n"
                   << "    return *value;\n}\n\n"
                   << "void " << native_name << "::_gdpp_static_" << name << "_release() {\n"
                   << "    std::lock_guard<std::mutex> lock(_gdpp_static_" << name << "_mutex());\n"
                   << "    delete _gdpp_static_" << name
                   << "_pointer().exchange(nullptr, std::memory_order_acq_rel);\n}\n\n";
        }
    }
    const auto emit_instance_initializers = [&]() {
        for (const auto& field : declaration.fields) {
            if (!field.is_constant && !field.is_static && !field.onready && field.initializer) {
                const bool editor_safe = editor_safe_initializer(*field.initializer);
                if (!editor_safe)
                    source << "    if (!gdpp_editor_hint) {\n";
                source << (editor_safe ? "    " : "        ") << sanitize_identifier(field.name)
                       << " = ";
                if (contains_preload(*field.initializer)) {
                    source << "_gdpp_preloaded_" << sanitize_identifier(field.name) << "()";
                } else {
                    source << emit_conversion(field.type, field.initializer->type,
                                              emit_expression(*field.initializer));
                }
                source << ";\n";
                if (!editor_safe)
                    source << "    }\n";
            }
        }
    };
    const bool has_instance_initializers =
        std::any_of(declaration.fields.begin(), declaration.fields.end(), [](const auto& field) {
            return !field.is_constant && !field.is_static && !field.onready && field.initializer;
        });
    const bool needs_editor_hint =
        std::any_of(declaration.fields.begin(), declaration.fields.end(), cached_preload_field) ||
        std::any_of(declaration.fields.begin(), declaration.fields.end(), [](const auto& field) {
            return !field.is_constant && !field.is_static && !field.onready && field.initializer &&
                   !editor_safe_initializer(*field.initializer);
        });
    const auto required =
        initializer == declaration.functions.end()
            ? std::ptrdiff_t{0}
            : std::count_if(initializer->parameters.begin(), initializer->parameters.end(),
                            [](const auto& parameter) { return !parameter.default_value; });
    if (has_instance_initializers &&
        (initializer == declaration.functions.end() || required != 0)) {
        in_function_body_ = true;
        source << native_name << "::" << native_name << "() {\n";
        if (needs_editor_hint)
            source << "    const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();\n";
        if (std::any_of(declaration.fields.begin(), declaration.fields.end(), cached_preload_field))
            source << "    if (!gdpp_editor_hint) _gdpp_preload_resources();\n";
        emit_instance_initializers();
        source << "}\n\n";
        in_function_body_ = false;
    }
    if (initializer != declaration.functions.end()) {
        in_function_body_ = true;
        source << native_name << "::" << native_name << '(';
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            const auto& parameter = initializer->parameters[index];
            source << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
        }
        source << ") {\n";
        source << "    const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();\n";
        if (std::any_of(declaration.fields.begin(), declaration.fields.end(), cached_preload_field))
            source << "    if (!gdpp_editor_hint) _gdpp_preload_resources();\n";
        emit_instance_initializers();
        source << "    if (gdpp_editor_hint) return;\n";
        source << "    _init(";
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            source << parameter_native_name(initializer->parameters[index]);
        }
        source << ");\n";
        source << "}\n\n";
        in_function_body_ = false;
    }
    for (const auto& field : declaration.fields) {
        if (cached_preload_field(field) && !field.is_static) {
            source << cpp_type(field.type) << "& " << native_name << "::_gdpp_preloaded_"
                   << sanitize_identifier(field.name) << "() {\n    static " << cpp_type(field.type)
                   << " value{};\n    return value;\n}\n\n";
        }
    }
    source << "void " << native_name << "::_gdpp_preload_resources() {\n";
    const bool has_cached_preloads =
        std::any_of(declaration.fields.begin(), declaration.fields.end(), cached_preload_field);
    if (has_cached_preloads) {
        source << "    if (gdpp::runtime::is_editor_hint()) return;\n";
        source << "    static std::once_flag once;\n    std::call_once(once, []() {\n";
    }
    for (const auto& field : declaration.fields) {
        if (!cached_preload_field(field))
            continue;
        source << (has_cached_preloads ? "        " : "    ");
        if (!field.is_static)
            source << "_gdpp_preloaded_";
        source << sanitize_identifier(field.name);
        if (!field.is_static)
            source << "()";
        source << " = "
               << emit_conversion(field.type, field.initializer->type,
                                  emit_expression(*field.initializer))
               << ";\n";
    }
    if (has_cached_preloads)
        source << "    });\n";
    source << "}\n\n";
    source << "void " << native_name << "::_gdpp_release_preloaded_resources() {\n";
    for (const auto& field : declaration.fields) {
        if (managed_constant_field(field)) {
            const auto name = sanitize_identifier(field.name);
            const auto storage = "_gdpp_constant_" + name + "_storage()";
            source << "    {\n"
                   << "        std::lock_guard<std::mutex> lock(_gdpp_constant_" << name
                   << "_mutex());\n"
                   << "        if (_gdpp_constant_" << name << "_ready()) {\n"
                   << "            " << storage << " = std::remove_reference_t<decltype(" << storage
                   << ")>{};\n"
                   << "            _gdpp_constant_" << name << "_ready() = false;\n"
                   << "        }\n"
                   << "    }\n";
            continue;
        }
        if (!cached_preload_field(field) && !(field.is_static && !field.is_constant))
            continue;
        const auto target = field.is_static
                                ? "_gdpp_static_" + sanitize_identifier(field.name) + "_storage()"
                                : "_gdpp_preloaded_" + sanitize_identifier(field.name) + "()";
        if (field.is_static)
            source << "    _gdpp_static_" << sanitize_identifier(field.name) << "_release();\n";
        else
            source << "    " << target << " = std::remove_reference_t<decltype(" << target
                   << ")>{};\n";
    }
    source << "}\n\n";
    source << "void " << native_name << "::_bind_methods() {\n";
    source << "    godot::ClassDB::bind_static_method(get_class_static(), "
              "godot::D_METHOD(\"_gdpp_preload_resources\"), &"
           << native_name << "::_gdpp_preload_resources);\n";
    std::unordered_map<std::string, std::size_t> inner_enum_member_counts;
    for (const auto& enumeration : declaration.enums) {
        for (const auto& entry : enumeration.entries)
            ++inner_enum_member_counts[entry.name];
    }
    for (const auto& enumeration : declaration.enums) {
        for (const auto& entry : enumeration.entries) {
            // GDExtension integer constants occupy a flat namespace per native class even when
            // an enum name is supplied. GDScript permits the same member name in multiple named
            // enums, so omit only the ambiguous reflection entries; generated C++ and exported
            // property hints still retain every enum member and value.
            if (inner_enum_member_counts[entry.name] != 1)
                continue;
            const auto value = enumeration.name.empty() ? enum_identifier(entry.name)
                                                        : sanitize_identifier(enumeration.name) +
                                                              "::" + enum_identifier(entry.name);
            source << "    godot::ClassDB::bind_integer_constant(get_class_static(), "
                   << godot_text_argument(enumeration.name) << ", "
                   << godot_text_argument(entry.name) << ", " << value << ");\n";
        }
    }
    for (const auto& function : declaration.functions) {
        if (function.name == "_init" || function.name == "_static_init" ||
            (!function.is_static && engine_virtual_for(function)))
            continue;
        source << "    godot::ClassDB::bind_" << (function.is_static ? "static_" : "") << "method(";
        if (function.is_static)
            source << "get_class_static(), ";
        source << "godot::D_METHOD(" << godot_text_argument(function.name);
        for (const auto& parameter : function.parameters)
            source << ", " << godot_text_argument(parameter.name);
        source << "), &" << native_name << "::" << sanitize_identifier(function.name)
               << emit_bound_parameter_defaults(function.parameters) << ");\n";
    }
    for (const auto& field : declaration.fields) {
        if (field.is_constant || field.is_static)
            continue;
        const auto name = sanitize_identifier(field.name);
        source << "    godot::ClassDB::bind_method(godot::D_METHOD(\"_gdpp_get_" << name << "\"), &"
               << native_name << "::_gdpp_get_" << name << ");\n"
               << "    godot::ClassDB::bind_method(godot::D_METHOD(\"_gdpp_set_" << name
               << "\", \"value\"), &" << native_name << "::_gdpp_set_" << name << ");\n"
               << "    ADD_PROPERTY(" << property_info(field, api_, script_symbols_)
               << ", \"_gdpp_set_" << name << "\", \"_gdpp_get_" << name << "\");\n";
    }
    for (const auto& signal : declaration.signals) {
        source << "    ADD_SIGNAL(godot::MethodInfo(" << godot_text_argument(signal.name);
        for (const auto& parameter : signal.parameters)
            source << ", godot::PropertyInfo(" << variant_type(parameter.type) << ", "
                   << godot_text_argument(parameter.name) << ")";
        source << "));\n";
    }
    if (std::any_of(declaration.functions.begin(), declaration.functions.end(),
                    [](const auto& function) { return function.name == "_static_init"; })) {
        source << "    _static_init();\n";
    }
    source << "}\n\n";
    for (const auto& field : declaration.fields) {
        if (field.is_constant)
            continue;
        const auto name = sanitize_identifier(field.name);
        source << cpp_type(field.type) << ' ' << native_name << "::_gdpp_get_" << name << "() {\n";
        if (field.getter && !field.getter->method.empty()) {
            source << "    return " << sanitize_identifier(field.getter->method) << "();\n";
        } else if (field.getter) {
            current_return_type_ = field.type;
            in_function_body_ = true;
            source << emit_statements(field.getter->body, 1);
            if (requires_native_fallback(field.getter->body))
                source << "    return {};\n";
        } else {
            source << "    return "
                   << (field.is_static ? "_gdpp_static_" + name + "_storage()" : name) << ";\n";
        }
        source << "}\n\nvoid " << native_name << "::_gdpp_set_" << name << '('
               << cpp_type(field.type) << ' '
               << (field.setter && field.setter->method.empty()
                       ? sanitize_identifier(field.setter->parameter)
                       : "value")
               << ") {\n";
        if (field.setter && !field.setter->method.empty()) {
            source << "    " << sanitize_identifier(field.setter->method) << "(value);\n";
        } else if (field.setter) {
            current_return_type_ = {TypeKind::void_type, "void"};
            in_function_body_ = true;
            source << emit_statements(field.setter->body, 1, 0,
                                      {{field.setter->parameter, field.type}});
        } else {
            source << "    " << (field.is_static ? "_gdpp_static_" + name + "_storage()" : name)
                   << " = value;\n";
        }
        source << "}\n\n";
        in_function_body_ = false;
    }
    for (const auto& function : declaration.functions) {
        current_return_type_ = function.return_type;
        current_coroutine_abi_ = coroutine_abi_for(function);
        current_coroutine_state_ = current_coroutine_abi_ ? "_gdpp_coroutine_state" : "";
        in_function_body_ = true;
        const auto* engine_virtual = engine_virtual_for(function);
        source << function_return_type(function) << ' ' << native_name
               << "::" << sanitize_identifier(function.name) << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            const auto& parameter = function.parameters[index];
            source << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
        }
        source << ')';
        if (engine_virtual && engine_virtual->is_const) {
            source << " const {\n    ";
            if (function.return_type.kind != TypeKind::void_type)
                source << "return ";
            source << "const_cast<" << native_name << "*>(this)->_gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    source << ", ";
                source << parameter_native_name(function.parameters[index]);
            }
            source << ");\n}\n\n"
                   << cpp_type(function.return_type) << ' ' << native_name << "::_gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    source << ", ";
                const auto& parameter = function.parameters[index];
                source << parameter_native_type(parameter) << ' '
                       << parameter_native_name(parameter);
            }
            source << ')';
        }
        source << " {\n" << emit_parameter_default_initializers(function.parameters, 1);
        if (current_coroutine_abi_) {
            source << "    const auto " << current_coroutine_state_
                   << " = gdpp::runtime::begin_coroutine(this);\n";
        }
        source << emit_statements(function.body, 1, 0, parameter_locals(function.parameters));
        if (!current_coroutine_abi_ && function.return_type.kind != TypeKind::void_type &&
            (requires_native_fallback(function.body) ||
             (function.return_type.is_dynamic() && native_statements_fall_through(function.body))))
            source << "    return {};\n";
        source << "}\n\n";
        in_function_body_ = false;
        current_coroutine_abi_ = false;
        current_coroutine_state_.clear();
    }
    local_function_parameters_ = previous_function_parameters;
    local_functions_ = previous_functions;
    current_native_class_name_ = previous_native_class_name;
    current_inner_script_ = previous_inner_script;
}

GeneratedUnit CodeGenerator::generate(const mir::Module& mir_module, const std::string& source_path,
                                      const std::string& native_class_suffix,
                                      const std::string& native_base_class,
                                      const std::string& native_base_header) const {
    if (!mir_module.hir) {
        diagnostics_.error("GDS5108", "C++ backend received a detached MIR module", {});
        return {};
    }
    const auto& module = *mir_module.hir;
    const std::filesystem::path path{source_path};
    const auto base_name = path.stem().string();
    GeneratedUnit unit;
    unit.is_abstract = module.is_abstract;
    match_counter_ = 0;
    temporary_counter_ = 0;
    current_source_path_ = source_path;
    current_return_type_ = {TypeKind::void_type, "void"};
    in_function_body_ = false;
    current_script_ = script_symbols_ ? script_symbols_->find_path(source_path) : nullptr;
    unit.script_class_name = current_script_
                                 ? current_script_->script_name
                                 : module.class_name.value_or(to_pascal_case(base_name));
    unit.script_class_name = sanitize_identifier(unit.script_class_name);
    unit.class_name = current_script_
                          ? current_script_->native_class_name
                          : "GDPPNative_" + unit.script_class_name + native_class_suffix;
    current_native_class_name_ = unit.class_name;
    inner_native_names_.clear();
    inner_godot_base_types_.clear();
    inner_base_names_.clear();
    inner_method_names_.clear();
    inner_ref_types_.clear();
    local_function_parameters_.clear();
    local_functions_.clear();
    for (const auto& function : module.functions) {
        local_functions_.emplace(function.name, &function);
        auto& parameters = local_function_parameters_[function.name];
        for (const auto& parameter : function.parameters)
            parameters.push_back(parameter.type);
    }
    std::vector<std::pair<std::string, const ir::Class*>> named_inner_classes;
    const auto collect_inner_classes = [&](const auto& self, const auto& declarations,
                                           const std::string& parent) -> void {
        for (const auto& declaration : declarations) {
            const auto qualified =
                parent.empty() ? declaration.name : parent + "." + declaration.name;
            named_inner_classes.emplace_back(qualified, &declaration);
            self(self, declaration.classes, qualified);
        }
    };
    collect_inner_classes(collect_inner_classes, module.classes, "");
    const auto find_named_inner = [&](std::string_view name) -> const ir::Class* {
        const auto found = std::find_if(named_inner_classes.begin(), named_inner_classes.end(),
                                        [&](const auto& value) { return value.first == name; });
        return found == named_inner_classes.end() ? nullptr : found->second;
    };
    const auto resolve_inner_base = [&](const std::string& owner,
                                        const std::string& base) -> std::string {
        if (find_named_inner(base))
            return base;
        const auto separator = owner.rfind('.');
        if (separator != std::string::npos) {
            const auto lexical = owner.substr(0, separator + 1) + base;
            if (find_named_inner(lexical))
                return lexical;
        }
        std::string unique;
        for (const auto& [qualified, declaration] : named_inner_classes) {
            static_cast<void>(declaration);
            const auto leaf_separator = qualified.rfind('.');
            const auto leaf = leaf_separator == std::string::npos
                                  ? qualified
                                  : qualified.substr(leaf_separator + 1);
            if (leaf != base)
                continue;
            if (!unique.empty())
                return {};
            unique = qualified;
        }
        return unique;
    };
    for (const auto& [qualified, declaration] : named_inner_classes) {
        std::string native_name = unit.class_name;
        std::size_t begin = 0;
        while (begin <= qualified.size()) {
            const auto separator = qualified.find('.', begin);
            const auto end = separator == std::string::npos ? qualified.size() : separator;
            native_name += "__" + sanitize_identifier(qualified.substr(begin, end - begin));
            if (separator == std::string::npos)
                break;
            begin = separator + 1;
        }
        if (script_symbols_ && current_script_) {
            if (const auto* symbol = script_symbols_->find_inner(*current_script_, qualified);
                symbol && !symbol->native_class_name.empty()) {
                native_name = symbol->native_class_name;
            }
        }
        inner_native_names_.emplace(qualified, native_name);
        if (!api_.find_class(declaration->base_type)) {
            const auto resolved = resolve_inner_base(qualified, declaration->base_type);
            if (!resolved.empty())
                inner_base_names_.emplace(qualified, resolved);
        }
        auto& methods = inner_method_names_[qualified];
        for (const auto& function : declaration->functions) {
            if (!function.is_static)
                methods.insert(function.name);
        }
        inner_ref_types_.insert(qualified);
    }
    for (const auto& [qualified, declaration] : named_inner_classes) {
        const ir::Class* current = declaration;
        std::string current_name = qualified;
        std::unordered_set<std::string> visited{qualified};
        while (!api_.find_class(current->base_type)) {
            const auto base = inner_base_names_.find(current_name);
            if (base == inner_base_names_.end() || !visited.insert(base->second).second)
                break;
            current_name = base->second;
            current = find_named_inner(current_name);
            if (!current)
                break;
        }
        inner_godot_base_types_.emplace(qualified, api_.find_class(current->base_type)
                                                       ? current->base_type
                                                       : std::string{"RefCounted"});
    }
    std::vector<std::pair<std::string, const ir::Class*>> ordered_inner_classes;
    std::unordered_set<std::string> ordered_inner_names;
    std::unordered_set<std::string> visiting_inner_names;
    const auto order_inner = [&](const auto& self, const std::string& qualified,
                                 const ir::Class& declaration) -> void {
        if (ordered_inner_names.find(qualified) != ordered_inner_names.end() ||
            !visiting_inner_names.insert(qualified).second) {
            return;
        }
        if (const auto base = inner_base_names_.find(qualified); base != inner_base_names_.end()) {
            if (const auto* base_declaration = find_named_inner(base->second))
                self(self, base->second, *base_declaration);
        }
        visiting_inner_names.erase(qualified);
        if (ordered_inner_names.insert(qualified).second)
            ordered_inner_classes.emplace_back(qualified, &declaration);
    };
    for (const auto& [qualified, declaration] : named_inner_classes)
        order_inner(order_inner, qualified, *declaration);
    for (const auto& [qualified, declaration] : ordered_inner_classes) {
        static_cast<void>(declaration);
        unit.inner_class_names.push_back(inner_native_names_.at(qualified));
    }
    const auto file_stem =
        current_script_
            ? std::filesystem::path{current_script_->header_file_name}.stem().stem().string()
            : to_snake_case(unit.script_class_name);
    detail_namespace_ = file_stem + "_gdpp_detail";
    unit.header_file_name =
        current_script_ ? current_script_->header_file_name : file_stem + ".gd.hpp";
    unit.source_file_name =
        current_script_
            ? std::filesystem::path{unit.header_file_name}.replace_extension(".cpp").string()
            : file_stem + ".gd.cpp";
    auto base = module.base_type.value_or("Node");
    if (native_base_class.empty() && !is_valid_base_type(base)) {
        diagnostics_.error("GDS3002",
                           "script-path inheritance requires semantic resolution and is not yet "
                           "supported",
                           module.span);
        base = "Node";
    }
    const auto base_cpp = native_base_class.empty() ? "godot::" + base : native_base_class;
    const auto initializer =
        std::find_if(module.functions.begin(), module.functions.end(),
                     [](const ir::Function& function) { return function.name == "_init"; });
    const bool has_instance_initializers =
        std::any_of(module.fields.begin(), module.fields.end(), [](const ir::Field& field) {
            return !field.is_constant && !field.is_static && !field.onready && field.initializer;
        });
    const bool is_autoload = current_script_ && !current_script_->autoload_name.empty();
    const auto ready =
        std::find_if(module.functions.begin(), module.functions.end(),
                     [](const ir::Function& function) { return function.name == "_ready"; });
    const bool has_onready_fields =
        std::any_of(module.fields.begin(), module.fields.end(),
                    [](const ir::Field& field) { return field.onready; });
    const auto godot_base_type = current_script_ ? current_script_->godot_base_type : base;
    const auto virtual_method_for = [&](const ir::Function& function) {
        if (function.is_static)
            return static_cast<const GodotMethodRecord*>(nullptr);
        const auto* method = api_.find_method(godot_base_type, function.name);
        return method && method->is_virtual ? method : nullptr;
    };
    const auto coroutine_abi_for = [&](const ir::Function& function) {
        return function.is_coroutine && !function.is_static && !virtual_method_for(function);
    };
    const auto function_return_type = [&](const ir::Function& function) {
        return coroutine_abi_for(function) ? std::string{"godot::Variant"}
                                           : cpp_type(function.return_type);
    };
    const auto overrides_script_method = [&](const ir::Function& function) {
        if (function.is_static || function.name == "_init" || !script_symbols_ ||
            !current_script_) {
            return false;
        }
        const auto inherited = script_symbols_->inherited_members(*current_script_);
        return std::any_of(inherited.begin(), inherited.end(), [&](const auto* member) {
            if (member->kind != ScriptMemberKind::function || member->is_static ||
                member->name != function.name || member->type != function.return_type ||
                member->is_coroutine != function.is_coroutine ||
                member->parameters.size() != function.parameters.size() ||
                member->default_parameters.size() != function.parameters.size()) {
                return false;
            }
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                const auto inherited_native = member->default_parameters[index]
                                                  ? std::string{"godot::Variant"}
                                                  : cpp_type(member->parameters[index]);
                if (inherited_native != parameter_native_type(function.parameters[index]))
                    return false;
            }
            return true;
        });
    };
    const auto function_native_name = [&](const ir::Function& function) {
        if (!current_script_ || function.is_static)
            return sanitize_identifier(function.name);
        const auto member =
            std::find_if(current_script_->members.begin(), current_script_->members.end(),
                         [&](const auto& candidate) {
                             return candidate.kind == ScriptMemberKind::function &&
                                    candidate.name == function.name;
                         });
        return member == current_script_->members.end()
                   ? sanitize_identifier(function.name)
                   : script_method_native_name(*current_script_, *member);
    };
    const auto function_parameter_type = [&](const ir::Function& function,
                                             const std::size_t index) {
        const auto& type = function.parameters[index].type;
        auto result = cpp_type(type);
        if (!virtual_method_for(function))
            return result;
        const bool ref_counted_object =
            type.kind == TypeKind::object &&
            (api_.inherits(type.name, "RefCounted") || !inner_cpp_type(type.name).empty());
        const bool builtin_reference =
            type.kind == TypeKind::string || type.kind == TypeKind::string_name ||
            type.kind == TypeKind::array || type.kind == TypeKind::dictionary ||
            type.kind == TypeKind::variant || type.kind == TypeKind::builtin;
        if (ref_counted_object || builtin_reference)
            return "const " + result + "&";
        return result;
    };

    std::ostringstream header;
    const auto native_types = collect_native_types(module, api_, script_symbols_);
    header << "// Generated by GDPP. Do not edit.\n"
           << "#pragma once\n\n";
    if (native_base_class.empty())
        header << "#include <" << header_for_base(base) << ">\n";
    else if (!native_base_header.empty())
        header << "#include \"" << native_base_header << "\"\n";
    for (const auto& type : native_types.builtins) {
        header << "#include <godot_cpp/variant/" << to_snake_case(type) << ".hpp>\n";
    }
    for (const auto& type : native_types.objects) {
        header << "#include <godot_cpp/classes/" << to_snake_case(type) << ".hpp>\n";
    }
    if (script_symbols_) {
        for (const auto& type : native_types.complete_scripts) {
            const auto* symbol = script_symbols_->find_global(type);
            if (symbol && symbol != current_script_ &&
                symbol->header_file_name != native_base_header)
                header << "#include \"" << symbol->header_file_name << "\"\n";
        }
        for (const auto& resource_path : native_types.complete_script_resources) {
            const auto* symbol = script_symbols_->find_path(resource_path);
            if (symbol && symbol != current_script_ &&
                symbol->header_file_name != native_base_header)
                header << "#include \"" << symbol->header_file_name << "\"\n";
        }
        for (const auto& type : native_types.scripts) {
            const auto* symbol = script_symbols_->find_global(type);
            if (symbol && symbol != current_script_ &&
                symbol->header_file_name != native_base_header &&
                native_types.complete_scripts.find(type) == native_types.complete_scripts.end())
                header << "class " << symbol->native_class_name << ";\n";
        }
        for (const auto& resource_path : native_types.script_resources) {
            const auto* symbol = script_symbols_->find_path(resource_path);
            if (symbol && symbol != current_script_ &&
                symbol->header_file_name != native_base_header &&
                native_types.complete_script_resources.find(resource_path) ==
                    native_types.complete_script_resources.end())
                header << "class " << symbol->native_class_name << ";\n";
        }
    }
    // ScriptResource selects pointer ownership from the actual generated base at compile time,
    // so every generated unit needs the Ref/RefCounted definitions even when its local AST does
    // not directly spell a ref-counted type.
    header << "#include <godot_cpp/classes/ref.hpp>\n"
           << "#include <godot_cpp/classes/ref_counted.hpp>\n"
           << "#include <godot_cpp/core/class_db.hpp>\n"
           << "#include <godot_cpp/core/error_macros.hpp>\n"
           << "#include <godot_cpp/core/math_defs.hpp>\n"
           << "#include <godot_cpp/core/memory.hpp>\n"
           << "#include <godot_cpp/core/object.hpp>\n"
           << "#include <godot_cpp/core/type_info.hpp>\n"
           << "#include <godot_cpp/variant/array.hpp>\n"
           << "#include <godot_cpp/variant/dictionary.hpp>\n"
           << "#include <godot_cpp/variant/utility_functions.hpp>\n"
           << "#include <godot_cpp/variant/variant.hpp>\n\n"
           << "#include <gdpp/runtime/variant_ops.hpp>\n\n"
           << "#include <cstdint>\n"
           << "#include <atomic>\n"
           << "#include <functional>\n"
           << "#include <initializer_list>\n"
           << "#include <memory>\n"
           << "#include <mutex>\n"
           << "#include <type_traits>\n"
           << "#include <utility>\n\n"
           << "namespace " << detail_namespace_ << " {\n"
           << "template <typename T> struct ScriptResource {\n"
           << "    operator godot::Variant() const {\n"
           << "        return godot::Variant(godot::StringName(T::get_class_static()));\n"
           << "    }\n"
           << "    template <typename... Args>\n"
           << "    static auto instantiate(Args &&...args) {\n"
           << "        if constexpr (std::is_base_of_v<godot::RefCounted, T>) {\n"
           << "            return godot::Ref<T>(memnew(T(std::forward<Args>(args)...)));\n"
           << "        } else {\n"
           << "            return memnew(T(std::forward<Args>(args)...));\n"
           << "        }\n"
           << "    }\n"
           << "};\n"
           << "template <typename T> struct InternalClassResource {\n"
           << "    template <typename... Args>\n"
           << "    static godot::Ref<T> instantiate(Args &&...args) {\n"
           << "        return godot::Ref<T>(memnew(T(std::forward<Args>(args)...)));\n"
           << "    }\n"
           << "};\n"
           << "inline godot::Dictionary make_dictionary(\n"
           << "    std::initializer_list<std::pair<godot::Variant, godot::Variant>> values) {\n"
           << "    godot::Dictionary result;\n"
           << "    for (const auto &entry : values) result[entry.first] = entry.second;\n"
           << "    return result;\n"
           << "}\n"
           << "} // namespace " << detail_namespace_ << "\n\n";
    for (const auto& [qualified, declaration] : ordered_inner_classes) {
        emit_inner_class_declaration(*declaration, header, inner_native_names_.at(qualified),
                                     qualified);
    }
    header << "class " << unit.class_name << " : public " << base_cpp << " {\n"
           << "    GDCLASS(" << unit.class_name << ", " << base_cpp << ")\n\n"
           << "public:\n";
    for (const auto& enumeration : module.enums) {
        if (enumeration.name.empty()) {
            for (const auto& entry : enumeration.entries) {
                header << "    inline static constexpr int64_t " << enum_identifier(entry.name)
                       << " = " << entry.value << ";\n";
            }
        } else {
            header << "    struct " << sanitize_identifier(enumeration.name) << " {\n";
            for (const auto& entry : enumeration.entries) {
                header << "        inline static constexpr int64_t " << enum_identifier(entry.name)
                       << " = " << entry.value << ";\n";
            }
            header << "    };\n";
        }
    }
    if (!module.enums.empty())
        header << '\n';
    for (const auto& variable : module.fields) {
        if (!variable.is_constant)
            continue;
        header << "    static const " << cpp_type(variable.type);
        if (managed_constant_field(variable))
            header << "& " << sanitize_identifier(variable.name) << "();\n";
        else
            header << ' ' << sanitize_identifier(variable.name) << ";\n";
    }
    if (std::any_of(module.fields.begin(), module.fields.end(),
                    [](const ir::Field& field) { return field.is_constant; }))
        header << '\n';
    if (initializer != module.functions.end()) {
        const auto required =
            std::count_if(initializer->parameters.begin(), initializer->parameters.end(),
                          [](const ir::Parameter& parameter) { return !parameter.default_value; });
        if (required != 0)
            header << "    " << unit.class_name
                   << (has_instance_initializers || is_autoload ? "();\n" : "() = default;\n");
        header << "    " << unit.class_name << '(';
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                header << ", ";
            const auto& parameter = initializer->parameters[index];
            header << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
            if (parameter.default_value)
                header << " = " << emit_parameter_default(parameter);
        }
        header << ");\n\n";
    } else if (has_instance_initializers || is_autoload) {
        header << "    " << unit.class_name << "();\n\n";
    }
    header << "protected:\n"
           << "    static void _bind_methods();\n\n";
    if (std::none_of(module.fields.begin(), module.fields.end(),
                     [](const ir::Field& field) { return !field.is_constant; }))
        header << "    // No script fields.\n";
    for (const auto& variable : module.fields) {
        if (variable.is_constant)
            continue;
        header << "    ";
        if (variable.is_static) {
            const auto name = sanitize_identifier(variable.name);
            const auto type = cpp_type(variable.type);
            header << "static std::atomic<" << type << "*>& _gdpp_static_" << name
                   << "_pointer();\n"
                   << "    static std::mutex& _gdpp_static_" << name << "_mutex();\n"
                   << "    static " << type << "& _gdpp_static_" << name << "_storage();\n"
                   << "    static void _gdpp_static_" << name << "_release();\n";
        } else {
            header << cpp_type(variable.type) << ' ' << sanitize_identifier(variable.name)
                   << "{};\n";
        }
    }
    for (const auto& variable : module.fields) {
        if (cached_preload_field(variable) && !variable.is_static) {
            header << "    static " << cpp_type(variable.type) << "& _gdpp_preloaded_"
                   << sanitize_identifier(variable.name) << "();\n";
        }
    }
    for (const auto& variable : module.fields) {
        if (!managed_constant_field(variable))
            continue;
        const auto name = sanitize_identifier(variable.name);
        header << "    static " << cpp_type(variable.type) << "& _gdpp_constant_" << name
               << "_storage();\n"
               << "    static bool& _gdpp_constant_" << name << "_ready();\n"
               << "    static std::mutex& _gdpp_constant_" << name << "_mutex();\n";
    }
    header << "\npublic:\n"
           << "    static void _gdpp_preload_resources();\n"
           << "    static void _gdpp_release_preloaded_resources();\n";
    for (const auto& variable : module.fields) {
        if (variable.is_constant)
            continue;
        const auto name = sanitize_identifier(variable.name);
        const auto setter_parameter = variable.setter && variable.setter->method.empty()
                                          ? sanitize_identifier(variable.setter->parameter)
                                          : "value";
        header << "    " << (variable.is_static ? "static " : "") << cpp_type(variable.type)
               << " _gdpp_get_" << name << "();\n"
               << "    " << (variable.is_static ? "static " : "") << "void _gdpp_set_" << name
               << "(" << cpp_type(variable.type) << ' ' << setter_parameter << ");\n";
    }
    if (std::any_of(module.fields.begin(), module.fields.end(),
                    [](const ir::Field& field) { return !field.is_constant; })) {
        header << '\n';
    }
    for (const auto& function : module.functions) {
        header << "    ";
        if (function.is_static)
            header << "static ";
        else
            header << "virtual ";
        header << function_return_type(function) << ' ' << function_native_name(function) << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0)
                header << ", ";
            const auto& parameter = function.parameters[index];
            header << (parameter.default_value ? parameter_native_type(parameter)
                                               : function_parameter_type(function, index))
                   << ' ' << parameter_native_name(parameter);
            if (parameter.default_value)
                header << " = " << emit_parameter_default(parameter);
        }
        header << ')';
        if (const auto* method = virtual_method_for(function); method && method->is_const)
            header << " const";
        if (virtual_method_for(function) || overrides_script_method(function))
            header << " override";
        header << ";\n";
        if (const auto* method = virtual_method_for(function); method && method->is_const) {
            header << "    " << cpp_type(function.return_type) << " _gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    header << ", ";
                const auto& parameter = function.parameters[index];
                header << function_parameter_type(function, index) << ' '
                       << sanitize_identifier(parameter.name);
            }
            header << ");\n";
        }
    }
    if (has_onready_fields && ready == module.functions.end()) {
        header << "    virtual void _ready()";
        if (const auto* method = api_.find_method(godot_base_type, "_ready");
            method && method->is_virtual) {
            header << " override";
        }
        header << ";\n";
    }
    header << "};\n";
    unit.header = header.str();

    std::ostringstream source;
    source << "// Generated by GDPP. Do not edit.\n"
           << "#include \"" << unit.header_file_name << "\"\n";
    if (script_symbols_) {
        std::set<std::string> dependency_headers;
        for (const auto& native_name : native_types.resolved_native_scripts) {
            const auto* symbol = script_symbols_->find_native_class(native_name);
            if (symbol && symbol != current_script_)
                dependency_headers.insert(symbol->header_file_name);
        }
        for (const auto& type : native_types.scripts) {
            const auto* symbol = script_symbols_->find_global(type);
            if (symbol && symbol != current_script_)
                dependency_headers.insert(symbol->header_file_name);
        }
        for (const auto& resource_path : native_types.script_resources) {
            const auto* symbol = script_symbols_->find_path(resource_path);
            if (symbol && symbol != current_script_)
                dependency_headers.insert(symbol->header_file_name);
        }
        if (current_script_) {
            const auto add_complete_script_type = [&](const Type& type) {
                if (type.kind != TypeKind::object)
                    return;
                const auto* symbol = script_symbols_->find_class(type.name);
                if (symbol && symbol != current_script_)
                    dependency_headers.insert(symbol->header_file_name);
            };
            // Super calls and checked downcasts may use a type declared only by an inherited
            // signature. Those types do not necessarily appear in the child AST, but C++
            // dynamic_cast requires a complete target in the generated source.
            for (const auto* member : script_symbols_->inherited_members(*current_script_)) {
                add_complete_script_type(member->type);
                for (const auto& parameter : member->parameters)
                    add_complete_script_type(parameter);
            }
        }
        for (const auto& header_file : dependency_headers)
            source << "#include \"" << header_file << "\"\n";
    }
    source << '\n';
    for (const auto& [qualified, declaration] : ordered_inner_classes) {
        emit_inner_class_definition(*declaration, source, inner_native_names_.at(qualified),
                                    qualified);
    }
    for (const auto& variable : module.fields) {
        if (!variable.is_constant)
            continue;
        const auto name = sanitize_identifier(variable.name);
        if (!managed_constant_field(variable)) {
            source << "const " << cpp_type(variable.type) << ' ' << unit.class_name << "::" << name
                   << " = ";
            if (variable.initializer && !variable.onready) {
                source << emit_conversion(variable.type, variable.initializer->type,
                                          emit_expression(*variable.initializer));
            } else {
                source << "{}";
            }
            source << ";\n";
            continue;
        }
        const auto type = cpp_type(variable.type);
        source << type << "& " << unit.class_name << "::_gdpp_constant_" << name
               << "_storage() {\n    static " << type << " value{};\n    return value;\n}\n\n"
               << "bool& " << unit.class_name << "::_gdpp_constant_" << name
               << "_ready() {\n    static bool value = false;\n    return value;\n}\n\n"
               << "std::mutex& " << unit.class_name << "::_gdpp_constant_" << name
               << "_mutex() {\n    static std::mutex value;\n    return value;\n}\n\n"
               << "const " << type << "& " << unit.class_name << "::" << name << "() {\n"
               << "    std::lock_guard<std::mutex> lock(_gdpp_constant_" << name << "_mutex());\n"
               << "    if (!_gdpp_constant_" << name << "_ready()) {\n"
               << "        _gdpp_constant_" << name << "_storage() = ";
        if (variable.initializer && !variable.onready) {
            source << emit_conversion(variable.type, variable.initializer->type,
                                      emit_expression(*variable.initializer));
        } else {
            source << "{}";
        }
        source << ";\n"
               << "        _gdpp_constant_" << name << "_ready() = true;\n"
               << "    }\n"
               << "    return _gdpp_constant_" << name << "_storage();\n}\n\n";
    }
    if (std::any_of(module.fields.begin(), module.fields.end(),
                    [](const ir::Field& field) { return field.is_constant; }))
        source << '\n';
    for (const auto& variable : module.fields) {
        if (!variable.is_constant && variable.is_static) {
            const auto name = sanitize_identifier(variable.name);
            const auto type = cpp_type(variable.type);
            source << "std::atomic<" << type << "*>& " << unit.class_name << "::_gdpp_static_"
                   << name << "_pointer() {\n    static std::atomic<" << type
                   << "*> value{nullptr};\n    return value;\n}\n\n"
                   << "std::mutex& " << unit.class_name << "::_gdpp_static_" << name
                   << "_mutex() {\n    static std::mutex value;\n    return value;\n}\n\n"
                   << type << "& " << unit.class_name << "::_gdpp_static_" << name
                   << "_storage() {\n"
                   << "    auto* value = _gdpp_static_" << name
                   << "_pointer().load(std::memory_order_acquire);\n"
                   << "    if (value) return *value;\n"
                   << "    std::lock_guard<std::mutex> lock(_gdpp_static_" << name << "_mutex());\n"
                   << "    value = _gdpp_static_" << name
                   << "_pointer().load(std::memory_order_relaxed);\n"
                   << "    if (!value) {\n        value = new " << type << "(";
            if (variable.initializer && !variable.onready) {
                source << emit_conversion(variable.type, variable.initializer->type,
                                          emit_expression(*variable.initializer));
            } else {
                source << "{}";
            }
            source << ");\n        _gdpp_static_" << name
                   << "_pointer().store(value, std::memory_order_release);\n    }\n"
                   << "    return *value;\n}\n\n"
                   << "void " << unit.class_name << "::_gdpp_static_" << name << "_release() {\n"
                   << "    std::lock_guard<std::mutex> lock(_gdpp_static_" << name << "_mutex());\n"
                   << "    delete _gdpp_static_" << name
                   << "_pointer().exchange(nullptr, std::memory_order_acq_rel);\n}\n\n";
        }
    }
    if (std::any_of(module.fields.begin(), module.fields.end(),
                    [](const ir::Field& field) { return !field.is_constant && field.is_static; })) {
        source << '\n';
    }
    const auto emit_instance_initializers = [&]() {
        for (const auto& variable : module.fields) {
            if (!variable.is_constant && !variable.is_static && !variable.onready &&
                variable.initializer) {
                const bool editor_safe = editor_safe_initializer(*variable.initializer);
                if (!editor_safe)
                    source << "    if (!gdpp_editor_hint) {\n";
                source << (editor_safe ? "    " : "        ") << sanitize_identifier(variable.name)
                       << " = ";
                if (contains_preload(*variable.initializer)) {
                    source << "_gdpp_preloaded_" << sanitize_identifier(variable.name) << "()";
                } else {
                    source << emit_conversion(variable.type, variable.initializer->type,
                                              emit_expression(*variable.initializer));
                }
                source << ";\n";
                if (!editor_safe)
                    source << "    }\n";
            }
        }
    };
    const auto required =
        initializer == module.functions.end()
            ? std::ptrdiff_t{0}
            : std::count_if(initializer->parameters.begin(), initializer->parameters.end(),
                            [](const auto& parameter) { return !parameter.default_value; });
    const bool needs_editor_hint =
        is_autoload ||
        std::any_of(module.fields.begin(), module.fields.end(), cached_preload_field) ||
        std::any_of(module.fields.begin(), module.fields.end(), [](const auto& field) {
            return !field.is_constant && !field.is_static && !field.onready && field.initializer &&
                   !editor_safe_initializer(*field.initializer);
        });
    const auto emit_autoload_registration = [&]() {
        if (is_autoload) {
            source << "    if (!gdpp_editor_hint) gdpp::runtime::register_autoload("
                   << godot_string_name(current_script_->autoload_name) << ", this);\n";
        }
    };
    if ((has_instance_initializers || is_autoload) &&
        (initializer == module.functions.end() || required != 0)) {
        in_function_body_ = true;
        source << unit.class_name << "::" << unit.class_name << "() {\n";
        if (needs_editor_hint)
            source << "    const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();\n";
        emit_autoload_registration();
        if (std::any_of(module.fields.begin(), module.fields.end(), cached_preload_field))
            source << "    if (!gdpp_editor_hint) _gdpp_preload_resources();\n";
        emit_instance_initializers();
        source << "}\n\n";
        in_function_body_ = false;
    }
    if (initializer != module.functions.end()) {
        in_function_body_ = true;
        source << unit.class_name << "::" << unit.class_name << '(';
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            const auto& parameter = initializer->parameters[index];
            source << parameter_native_type(parameter) << ' ' << parameter_native_name(parameter);
        }
        source << ") {\n";
        source << "    const bool gdpp_editor_hint = gdpp::runtime::is_editor_hint();\n";
        emit_autoload_registration();
        if (std::any_of(module.fields.begin(), module.fields.end(), cached_preload_field))
            source << "    if (!gdpp_editor_hint) _gdpp_preload_resources();\n";
        emit_instance_initializers();
        source << "    if (gdpp_editor_hint) return;\n";
        source << "    _init(";
        for (std::size_t index = 0; index < initializer->parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            source << parameter_native_name(initializer->parameters[index]);
        }
        source << ");\n";
        source << "}\n\n";
        in_function_body_ = false;
    }
    for (const auto& variable : module.fields) {
        if (cached_preload_field(variable) && !variable.is_static) {
            source << cpp_type(variable.type) << "& " << unit.class_name << "::_gdpp_preloaded_"
                   << sanitize_identifier(variable.name) << "() {\n    static "
                   << cpp_type(variable.type) << " value{};\n    return value;\n}\n\n";
        }
    }
    source << "void " << unit.class_name << "::_gdpp_preload_resources() {\n";
    const bool has_cached_preloads =
        std::any_of(module.fields.begin(), module.fields.end(), cached_preload_field);
    if (has_cached_preloads) {
        source << "    if (gdpp::runtime::is_editor_hint()) return;\n";
        source << "    static std::once_flag once;\n    std::call_once(once, []() {\n";
    }
    for (const auto& variable : module.fields) {
        if (!cached_preload_field(variable))
            continue;
        source << (has_cached_preloads ? "        " : "    ");
        if (!variable.is_static)
            source << "_gdpp_preloaded_";
        source << sanitize_identifier(variable.name);
        if (!variable.is_static)
            source << "()";
        source << " = "
               << emit_conversion(variable.type, variable.initializer->type,
                                  emit_expression(*variable.initializer))
               << ";\n";
    }
    if (has_cached_preloads)
        source << "    });\n";
    source << "}\n\n";
    source << "void " << unit.class_name << "::_gdpp_release_preloaded_resources() {\n";
    for (const auto& variable : module.fields) {
        if (managed_constant_field(variable)) {
            const auto name = sanitize_identifier(variable.name);
            const auto storage = "_gdpp_constant_" + name + "_storage()";
            source << "    {\n"
                   << "        std::lock_guard<std::mutex> lock(_gdpp_constant_" << name
                   << "_mutex());\n"
                   << "        if (_gdpp_constant_" << name << "_ready()) {\n"
                   << "            " << storage << " = std::remove_reference_t<decltype(" << storage
                   << ")>{};\n"
                   << "            _gdpp_constant_" << name << "_ready() = false;\n"
                   << "        }\n"
                   << "    }\n";
            continue;
        }
        if (!cached_preload_field(variable) && !(variable.is_static && !variable.is_constant))
            continue;
        const auto target =
            variable.is_static ? "_gdpp_static_" + sanitize_identifier(variable.name) + "_storage()"
                               : "_gdpp_preloaded_" + sanitize_identifier(variable.name) + "()";
        if (variable.is_static)
            source << "    _gdpp_static_" << sanitize_identifier(variable.name) << "_release();\n";
        else
            source << "    " << target << " = std::remove_reference_t<decltype(" << target
                   << ")>{};\n";
    }
    source << "}\n\n";
    source << "void " << unit.class_name << "::_bind_methods() {\n";
    source << "    godot::ClassDB::bind_static_method(get_class_static(), "
              "godot::D_METHOD(\"_gdpp_preload_resources\"), &"
           << unit.class_name << "::_gdpp_preload_resources);\n";
    std::unordered_map<std::string, std::size_t> enum_member_counts;
    for (const auto& enumeration : module.enums) {
        for (const auto& entry : enumeration.entries)
            ++enum_member_counts[entry.name];
    }
    for (const auto& enumeration : module.enums) {
        for (const auto& entry : enumeration.entries) {
            if (enum_member_counts[entry.name] != 1)
                continue;
            const auto value = enumeration.name.empty() ? enum_identifier(entry.name)
                                                        : sanitize_identifier(enumeration.name) +
                                                              "::" + enum_identifier(entry.name);
            source << "    godot::ClassDB::bind_integer_constant(get_class_static(), "
                   << godot_text_argument(enumeration.name) << ", "
                   << godot_text_argument(entry.name) << ", " << value << ");\n";
        }
    }
    for (const auto& variable : module.fields) {
        for (const auto& group : variable.property_groups) {
            const auto group_name =
                group.arguments.empty() ? std::string{} : group.arguments[0].value;
            const auto prefix =
                group.arguments.size() < 2 ? std::string{} : group.arguments[1].value;
            if (group.name == "export_category") {
                source << "    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::NIL, "
                       << godot_text_argument(group_name)
                       << ", godot::PROPERTY_HINT_NONE, godot::String(), "
                          "godot::PROPERTY_USAGE_CATEGORY), \"\", \"\");\n";
            } else {
                source << "    " << (group.name == "export_subgroup" ? "ADD_SUBGROUP" : "ADD_GROUP")
                       << "(" << godot_text_argument(group_name) << ", "
                       << godot_text_argument(prefix) << ");\n";
            }
        }
        if (!is_bound_property(variable))
            continue;
        const auto name = sanitize_identifier(variable.name);
        source << "    godot::ClassDB::bind_method(godot::D_METHOD(\"_gdpp_get_" << name << "\"), &"
               << unit.class_name << "::_gdpp_get_" << name << ");\n"
               << "    godot::ClassDB::bind_method(godot::D_METHOD(\"_gdpp_set_" << name
               << "\", \"value\"), &" << unit.class_name << "::_gdpp_set_" << name << ");\n"
               << "    ADD_PROPERTY(" << property_info(variable, api_, script_symbols_)
               << ", \"_gdpp_set_" << name << "\", \"_gdpp_get_" << name << "\");\n";
    }
    for (const auto& function : module.functions) {
        // Engine callbacks are C++ virtual overrides. Registering them again as
        // ordinary extension methods conflicts with godot-cpp's virtual-method
        // registry and can crash when a large extension is initialized.
        if (function.name == "_init" || function.name == "_static_init" ||
            (!function.is_static && virtual_method_for(function)))
            continue;
        source << "    godot::ClassDB::bind_" << (function.is_static ? "static_" : "") << "method(";
        if (function.is_static)
            source << "get_class_static(), ";
        source << "godot::D_METHOD(" << godot_text_argument(function.name);
        for (const auto& parameter : function.parameters) {
            source << ", " << godot_text_argument(parameter.name);
        }
        source << "), &" << unit.class_name << "::" << function_native_name(function)
               << emit_bound_parameter_defaults(function.parameters) << ");\n";
    }
    for (const auto& signal : module.signals) {
        source << "    ADD_SIGNAL(godot::MethodInfo(" << godot_text_argument(signal.name);
        for (const auto& parameter : signal.parameters) {
            source << ", godot::PropertyInfo(" << variant_type(parameter.type) << ", "
                   << godot_text_argument(parameter.name) << ")";
        }
        source << "));\n";
    }
    if (std::any_of(module.functions.begin(), module.functions.end(),
                    [](const auto& function) { return function.name == "_static_init"; })) {
        source << "    _static_init();\n";
    }
    source << "}\n\n";
    for (const auto& variable : module.fields) {
        if (variable.is_constant)
            continue;
        const auto name = sanitize_identifier(variable.name);
        source << cpp_type(variable.type) << ' ' << unit.class_name << "::_gdpp_get_" << name
               << "() {\n";
        if (variable.getter) {
            if (!variable.getter->method.empty()) {
                source << "    return " << sanitize_identifier(variable.getter->method) << "();\n";
            } else {
                current_return_type_ = variable.type;
                in_function_body_ = true;
                source << emit_statements(variable.getter->body, 1);
                in_function_body_ = false;
                if (requires_native_fallback(variable.getter->body))
                    source << "    return {};\n";
            }
        } else {
            source << "    return "
                   << (variable.is_static ? "_gdpp_static_" + name + "_storage()" : name) << ";\n";
        }
        const auto setter_parameter = variable.setter && variable.setter->method.empty()
                                          ? sanitize_identifier(variable.setter->parameter)
                                          : "value";
        source << "}\n\nvoid " << unit.class_name << "::_gdpp_set_" << name << '('
               << cpp_type(variable.type) << ' ' << setter_parameter << ") {\n";
        if (variable.setter) {
            if (!variable.setter->method.empty()) {
                source << "    " << sanitize_identifier(variable.setter->method) << '('
                       << setter_parameter << ");\n";
            } else {
                current_return_type_ = {TypeKind::void_type, "void"};
                in_function_body_ = true;
                source << emit_statements(variable.setter->body, 1, 0,
                                          {{variable.setter->parameter, variable.type}});
                in_function_body_ = false;
            }
        } else {
            source << "    " << (variable.is_static ? "_gdpp_static_" + name + "_storage()" : name)
                   << " = value;\n";
        }
        source << "}\n\n";
    }
    const auto mir_owner = module.class_name.value_or("<script>");
    for (const auto& function : module.functions) {
        current_return_type_ = function.return_type;
        current_coroutine_abi_ = coroutine_abi_for(function);
        current_coroutine_state_ = current_coroutine_abi_ ? "_gdpp_coroutine_state" : "";
        in_function_body_ = true;
        const auto* engine_virtual = virtual_method_for(function);
        source << function_return_type(function) << ' ' << unit.class_name
               << "::" << function_native_name(function) << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0)
                source << ", ";
            const auto& parameter = function.parameters[index];
            source << (parameter.default_value ? parameter_native_type(parameter)
                                               : function_parameter_type(function, index))
                   << ' ' << parameter_native_name(parameter);
        }
        source << ')';
        if (engine_virtual && engine_virtual->is_const) {
            source << " const {\n    ";
            if (function.return_type.kind != TypeKind::void_type)
                source << "return ";
            source << "const_cast<" << unit.class_name << "*>(this)->_gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    source << ", ";
                source << sanitize_identifier(function.parameters[index].name);
            }
            source << ");\n}\n\n"
                   << cpp_type(function.return_type) << ' ' << unit.class_name << "::_gdpp_mutable_"
                   << sanitize_identifier(function.name) << '(';
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (index != 0)
                    source << ", ";
                const auto& parameter = function.parameters[index];
                source << function_parameter_type(function, index) << ' '
                       << sanitize_identifier(parameter.name);
            }
            source << ')';
        }
        source << " {\n";
        source << emit_parameter_default_initializers(function.parameters, 1);
        if (current_coroutine_abi_) {
            source << "    const auto " << current_coroutine_state_
                   << " = gdpp::runtime::begin_coroutine(this);\n";
        }
        if (function.name == "_ready") {
            for (const auto& field : module.fields) {
                if (field.onready && field.initializer) {
                    source << "    " << sanitize_identifier(field.name) << " = "
                           << emit_conversion(field.type, field.initializer->type,
                                              emit_expression(*field.initializer))
                           << ";\n";
                }
            }
        }
        const auto mir_name = mir_owner + "::" + function.name;
        const auto mir_function = std::find_if(
            mir_module.functions.begin(), mir_module.functions.end(),
            [&](const mir::Function& candidate) {
                return candidate.role == mir::FunctionRole::method && candidate.name == mir_name;
            });
        if (mir_function != mir_module.functions.end() &&
            can_emit_flat_async(function, *mir_function)) {
            source << emit_flat_async(*mir_function, 1);
        } else {
            source << emit_statements(function.body, 1, 0, parameter_locals(function.parameters));
        }
        if (!current_coroutine_abi_ && function.return_type.kind != TypeKind::void_type &&
            (requires_native_fallback(function.body) ||
             (function.return_type.is_dynamic() && native_statements_fall_through(function.body))))
            source << "    return {};\n";
        source << "}\n\n";
        in_function_body_ = false;
        current_coroutine_abi_ = false;
        current_coroutine_state_.clear();
    }
    if (has_onready_fields && ready == module.functions.end()) {
        current_return_type_ = {TypeKind::void_type, "void"};
        in_function_body_ = true;
        source << "void " << unit.class_name << "::_ready() {\n";
        for (const auto& field : module.fields) {
            if (field.onready && field.initializer) {
                source << "    " << sanitize_identifier(field.name) << " = "
                       << emit_conversion(field.type, field.initializer->type,
                                          emit_expression(*field.initializer))
                       << ";\n";
            }
        }
        source << "}\n\n";
        in_function_body_ = false;
    }
    unit.source = source.str();
    return unit;
}

} // namespace gdpp
