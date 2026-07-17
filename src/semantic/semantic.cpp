#include "gdpp/semantic.hpp"

#include "gdpp/constant_evaluator.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace gdpp {
namespace {

const Type unknown_type{TypeKind::unknown, "unknown"};
const Type variant_type{TypeKind::variant, "Variant"};
const Type void_type{TypeKind::void_type, "void"};

bool is_number_literal(const ast::Expression& expression) {
    if (expression.kind() == ast::ExpressionKind::literal) {
        return expression.literal_kind() == ast::LiteralKind::integer ||
               expression.literal_kind() == ast::LiteralKind::floating;
    }
    return expression.kind() == ast::ExpressionKind::unary &&
           (expression.value() == "+" || expression.value() == "-") &&
           expression.operand_count() == 1 && is_number_literal(*expression.operand(0));
}

bool is_string_literal(const ast::Expression& expression) {
    return expression.kind() == ast::ExpressionKind::literal &&
           expression.literal_kind() == ast::LiteralKind::string;
}

bool has_property_annotation(const ast::VariableDeclaration& variable) {
    return std::any_of(variable.annotations.begin(), variable.annotations.end(),
                       [](const ast::Annotation& annotation) {
                           return annotation.name != "onready" &&
                                  annotation.name != "warning_ignore" &&
                                  annotation.name != "export_group" &&
                                  annotation.name != "export_subgroup" &&
                                  annotation.name != "export_category";
                       });
}

std::optional<Type> implied_export_property_type(const ast::VariableDeclaration& variable) {
    for (const auto& annotation : variable.annotations) {
        if (annotation.name == "export_color_no_alpha")
            return Type{TypeKind::builtin, "Color"};
        if (annotation.name == "export_node_path")
            return Type{TypeKind::builtin, "NodePath"};
        if (annotation.name == "export_tool_button")
            return Type{TypeKind::builtin, "Callable"};
        if (annotation.name == "export_flags" || annotation.name == "export_flags_2d_render" ||
            annotation.name == "export_flags_2d_physics" ||
            annotation.name == "export_flags_2d_navigation" ||
            annotation.name == "export_flags_3d_render" ||
            annotation.name == "export_flags_3d_physics" ||
            annotation.name == "export_flags_3d_navigation") {
            return Type{TypeKind::integer, "int"};
        }
        if (annotation.name == "export_file" || annotation.name == "export_file_path" ||
            annotation.name == "export_global_file" || annotation.name == "export_dir" ||
            annotation.name == "export_global_dir" || annotation.name == "export_multiline" ||
            annotation.name == "export_placeholder") {
            return Type{TypeKind::string, "String"};
        }
    }
    return std::nullopt;
}

bool contains_value_return(const std::vector<ast::Statement>& statements) {
    for (const auto& statement : statements) {
        if (statement.kind() == ast::StatementKind::return_statement && statement.expression())
            return true;
        if (contains_value_return(statement.body()) || contains_value_return(statement.else_body()))
            return true;
        for (const auto& branch : statement.match_branches()) {
            if (contains_value_return(branch.body))
                return true;
        }
    }
    return false;
}

void collect_structured_awaits(const std::vector<ast::Statement>& statements,
                               std::unordered_set<const ast::Statement*>& result) {
    for (const auto& statement : statements) {
        if (statement.kind() == ast::StatementKind::await_statement ||
            statement.kind() == ast::StatementKind::await_variable) {
            result.insert(&statement);
            continue;
        }
        if (statement.kind() == ast::StatementKind::if_statement ||
            statement.kind() == ast::StatementKind::while_statement ||
            statement.kind() == ast::StatementKind::for_statement) {
            collect_structured_awaits(statement.body(), result);
            collect_structured_awaits(statement.else_body(), result);
        }
        for (const auto& branch : statement.match_branches())
            collect_structured_awaits(branch.body, result);
    }
}

struct ProjectEnumLookup {
    const ScriptClassSymbol* owner{nullptr};
    const ScriptEnumSymbol* enumeration{nullptr};
};

ProjectEnumLookup find_project_enum(const ScriptSymbolTable* symbols, const std::string& name) {
    if (!symbols)
        return {};
    const auto separator = name.find('.');
    if (separator == std::string::npos || name.find('.', separator + 1) != std::string::npos)
        return {};
    const auto* owner = symbols->find_global(name.substr(0, separator));
    if (!owner)
        return {};
    return {owner, symbols->find_enum(*owner, name.substr(separator + 1))};
}

struct ExternalEnumLookup {
    const ExternalClassSymbol* owner{nullptr};
    const ScriptEnumSymbol* enumeration{nullptr};
};

ExternalEnumLookup find_external_enum(const ScriptSymbolTable* symbols, const std::string& name) {
    if (!symbols)
        return {};
    const auto separator = name.find('.');
    if (separator == std::string::npos || name.find('.', separator + 1) != std::string::npos)
        return {};
    const auto* owner = symbols->find_external(name.substr(0, separator));
    if (!owner)
        return {};
    return {owner, symbols->find_external_enum(*owner, name.substr(separator + 1))};
}

std::string builtin_operator_type(const Type& type) {
    switch (type.kind) {
    case TypeKind::nil:
        return "Nil";
    case TypeKind::boolean:
        return "bool";
    case TypeKind::integer:
    case TypeKind::enumeration:
        return "int";
    case TypeKind::floating:
        return "float";
    case TypeKind::string:
        return "String";
    case TypeKind::string_name:
        return "StringName";
    case TypeKind::array:
        return "Array";
    case TypeKind::dictionary:
        return "Dictionary";
    case TypeKind::builtin:
        return type.name;
    case TypeKind::object:
        return "Object";
    case TypeKind::variant:
    case TypeKind::unknown:
        return "Variant";
    case TypeKind::void_type:
    case TypeKind::script_resource:
        return {};
    }
    return {};
}

} // namespace

Type SemanticModel::type_of(const ast::Expression& expression) const {
    const auto found = expression_types_.find(&expression);
    return found == expression_types_.end() ? unknown_type : found->second;
}

Type SemanticModel::type_of(const ast::VariableDeclaration& declaration) const {
    const auto found = variable_types_.find(&declaration);
    return found == variable_types_.end() ? unknown_type : found->second;
}

Type SemanticModel::property_type_of(const ast::VariableDeclaration& declaration) const {
    const auto found = property_types_.find(&declaration);
    return found == property_types_.end() ? type_of(declaration) : found->second;
}

Type SemanticModel::type_of(const ast::Statement& statement) const {
    const auto found = local_types_.find(&statement);
    return found == local_types_.end() ? unknown_type : found->second;
}

Type SemanticModel::type_of(const ast::Parameter& parameter) const {
    const auto found = parameter_types_.find(&parameter);
    return found == parameter_types_.end() ? unknown_type : found->second;
}

Type SemanticModel::return_type_of(const ast::FunctionDeclaration& function) const {
    const auto found = function_return_types_.find(&function);
    return found == function_return_types_.end() ? unknown_type : found->second;
}

Type SemanticModel::return_type_of(const ast::LambdaExpression& function) const {
    const auto found = lambda_return_types_.find(&function);
    return found == lambda_return_types_.end() ? unknown_type : found->second;
}

bool SemanticModel::owner_bound(const ast::LambdaExpression& function) const noexcept {
    return owner_bound_lambdas_.find(&function) != owner_bound_lambdas_.end();
}

std::int64_t SemanticModel::value_of(const ast::EnumEntry& entry) const {
    const auto found = enum_values_.find(&entry);
    return found == enum_values_.end() ? 0 : found->second;
}

const Symbol* SemanticModel::symbol_of(const ast::Expression& expression) const noexcept {
    const auto found = referenced_symbols_.find(&expression);
    return found == referenced_symbols_.end() ? nullptr : &found->second;
}

const ApiResolution*
SemanticModel::api_resolution_of(const ast::Expression& expression) const noexcept {
    const auto found = api_resolutions_.find(&expression);
    return found == api_resolutions_.end() ? nullptr : &found->second;
}

void SemanticAnalyzer::declare(Symbol symbol) {
    auto& scope = scopes_.back();
    const auto existing = scope.find(symbol.name);
    if (existing != scope.end()) {
        diagnostics_.error("GDS4001", "duplicate declaration of '" + symbol.name + "'",
                           symbol.declaration);
        return;
    }
    scope.emplace(symbol.name, std::move(symbol));
}

const Symbol* SemanticAnalyzer::resolve(const std::string& name) const noexcept {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end())
            return &found->second;
    }
    return nullptr;
}

void SemanticAnalyzer::require_assignable(const Type& target, const Type& source, SourceSpan span,
                                          const std::string& context) {
    const bool inherited_object = target.kind == TypeKind::object &&
                                  source.kind == TypeKind::object &&
                                  api_.inherits(source.name, target.name);
    const bool inherited_script = script_symbols_ && target.kind == TypeKind::object &&
                                  source.kind == TypeKind::object &&
                                  script_symbols_->inherits(source.name, target.name);
    const auto* source_script = script_symbols_ && source.kind == TypeKind::object
                                    ? script_symbols_->find_class(source.name)
                                    : nullptr;
    if (!source_script && current_script_ && source.kind == TypeKind::object &&
        source.name == current_script_->script_name) {
        source_script = current_script_;
    }
    const bool script_inherits_engine = source_script && target.kind == TypeKind::object &&
                                        api_.inherits(source_script->godot_base_type, target.name);
    const bool current_script_inherits = source_script && script_symbols_ &&
                                         target.kind == TypeKind::object &&
                                         script_symbols_->inherits(*source_script, target.name);
    const auto* target_script = script_symbols_ && target.kind == TypeKind::object
                                    ? script_symbols_->find_class(target.name)
                                    : nullptr;
    const bool safe_object_downcast =
        target.kind == TypeKind::object && source.kind == TypeKind::object &&
        (api_.inherits(target.name, source.name) ||
         (target_script && api_.inherits(target_script->godot_base_type, source.name)));
    const bool checked_script_downcast = script_symbols_ && target.kind == TypeKind::object &&
                                         source.kind == TypeKind::object &&
                                         script_symbols_->inherits(target.name, source.name);
    const bool same_project_enum =
        current_script_ && target.kind == TypeKind::enumeration &&
        source.kind == TypeKind::enumeration &&
        (target.name == current_script_->script_name + "." + source.name ||
         source.name == current_script_->script_name + "." + target.name);
    bool constructor_conversion = false;
    if (target.kind == TypeKind::builtin) {
        for (std::size_t occurrence = 0;; ++occurrence) {
            const auto* constructor = api_.find_constructor(target.name, 1, occurrence);
            if (!constructor)
                break;
            const auto* argument = api_.argument(*constructor, 0);
            if (argument && is_assignable(type_from_godot_api(argument->type), source)) {
                constructor_conversion = true;
                break;
            }
        }
    }
    const bool packed_color_conversion = target.kind == TypeKind::builtin &&
                                         target.name == "Color" && source.kind == TypeKind::integer;
    const bool object_rid_conversion = target.kind == TypeKind::builtin && target.name == "RID" &&
                                       source.kind == TypeKind::object &&
                                       api_.find_method(source.name, "get_rid");
    if (!is_assignable(target, source) && !inherited_object && !inherited_script &&
        !script_inherits_engine && !current_script_inherits && !safe_object_downcast &&
        !checked_script_downcast && !same_project_enum && !constructor_conversion &&
        !packed_color_conversion && !object_rid_conversion) {
        diagnostics_.error("GDS4002",
                           context + ": cannot assign " + source.display_name() + " to " +
                               target.display_name(),
                           span);
    }
}

void SemanticAnalyzer::validate_script_call(const ScriptMemberSymbol& member,
                                            const std::vector<Type>& arguments, SourceSpan span) {
    if (member.kind != ScriptMemberKind::function) {
        diagnostics_.error("GDS4053", "script member '" + member.name + "' is not callable", span);
        return;
    }
    if (arguments.size() < member.required_arguments ||
        (!member.is_vararg && arguments.size() > member.parameters.size())) {
        diagnostics_.error(
            "GDS4054",
            "script method '" + member.name + "' expects " +
                std::to_string(member.required_arguments) +
                (member.required_arguments == member.parameters.size() && !member.is_vararg
                     ? " argument(s)"
                 : member.is_vararg
                     ? " or more argument(s)"
                     : " to " + std::to_string(member.parameters.size()) + " argument(s)") +
                ", got " + std::to_string(arguments.size()),
            span);
    }
    const auto checked = std::min(arguments.size(), member.parameters.size());
    for (std::size_t index = 0; index < checked; ++index) {
        require_assignable(member.parameters[index], arguments[index], span,
                           "argument " + std::to_string(index + 1) + " of '" + member.name + "'");
    }
}

const ScriptInnerClassSymbol*
SemanticAnalyzer::find_inner_class(const std::string& name) const noexcept {
    const auto separator = name.rfind('.');
    const auto local_name = separator == std::string::npos ? name : name.substr(separator + 1);
    if (const auto found = local_inner_classes_.find(local_name);
        found != local_inner_classes_.end()) {
        return &found->second;
    }
    return script_symbols_ && current_script_ ? script_symbols_->find_inner(*current_script_, name)
                                              : nullptr;
}

void SemanticAnalyzer::record_script_dependency(const ScriptClassSymbol* dependency) {
    if (dependency && (!current_script_ || dependency->path != current_script_->path))
        model_.referenced_script_paths_.insert(dependency->path);
}

Type SemanticAnalyzer::declared_or_inferred(const std::optional<std::string>& annotation,
                                            const ast::ExpressionPtr& initializer) {
    if (annotation.has_value())
        return type_from_name(*annotation);
    return initializer ? analyze_expression(*initializer) : variant_type;
}

Type SemanticAnalyzer::type_from_name(const std::string& name, SourceSpan span) {
    if (enum_types_.find(name) != enum_types_.end()) {
        if (current_inner_class_)
            return {TypeKind::enumeration, current_inner_class_->name + "." + name};
        if (current_script_)
            return {TypeKind::enumeration, current_script_->script_name + "." + name};
        return {TypeKind::enumeration, name};
    }
    if (api_.has_global_enum(name))
        return {TypeKind::enumeration, name};
    if (const auto separator = name.rfind('.');
        separator != std::string::npos &&
        api_.has_class_enum(name.substr(0, separator), name.substr(separator + 1))) {
        return {TypeKind::enumeration, name};
    }
    if (const auto project_enum = find_project_enum(script_symbols_, name);
        project_enum.enumeration) {
        record_script_dependency(project_enum.owner);
        return {TypeKind::enumeration, name};
    }
    if (const auto external_enum = find_external_enum(script_symbols_, name);
        external_enum.enumeration) {
        model_.referenced_extension_abis_.insert(external_enum.owner->provider_abi);
        return {TypeKind::enumeration, name};
    }
    const auto type = type_from_annotation(name);
    const auto* project_type = script_symbols_ ? script_symbols_->find_global(name) : nullptr;
    const auto* external_type = script_symbols_ ? script_symbols_->find_external(name) : nullptr;
    if (external_type)
        model_.referenced_extension_abis_.insert(external_type->provider_abi);
    record_script_dependency(project_type);
    if (type.kind == TypeKind::object && !api_.find_class(name) && !find_inner_class(name) &&
        !project_type && !external_type) {
        diagnostics_.error("GDS4059", "unknown Godot or project script type '" + name + "'", span);
    }
    return type;
}

Type SemanticAnalyzer::container_element_type(const Type& container, SourceSpan span) {
    if (container.kind == TypeKind::integer)
        return {TypeKind::integer, "int"};
    if (container.kind == TypeKind::string)
        return {TypeKind::string, "String"};
    if (container.is_packed_array())
        return packed_array_element_type(container);
    if (container.kind != TypeKind::array)
        return variant_type;
    constexpr std::string_view prefix{"Array["};
    if (container.name.size() <= prefix.size() ||
        container.name.compare(0, prefix.size(), prefix) != 0 || container.name.back() != ']') {
        return variant_type;
    }
    return type_from_name(
        container.name.substr(prefix.size(), container.name.size() - prefix.size() - 1), span);
}

Type SemanticAnalyzer::analyze_expression(const ast::Expression& expression) {
    Type result = unknown_type;
    switch (expression.kind()) {
    case ast::ExpressionKind::literal:
        switch (expression.literal_kind()) {
        case ast::LiteralKind::nil:
            result = {TypeKind::nil, "null"};
            break;
        case ast::LiteralKind::boolean:
            result = {TypeKind::boolean, "bool"};
            break;
        case ast::LiteralKind::integer:
            result = {TypeKind::integer, "int"};
            break;
        case ast::LiteralKind::floating:
            result = {TypeKind::floating, "float"};
            break;
        case ast::LiteralKind::string:
            result = {TypeKind::string, "String"};
            break;
        case ast::LiteralKind::string_name:
            result = {TypeKind::string_name, "StringName"};
            break;
        case ast::LiteralKind::node_path:
            result = {TypeKind::builtin, "NodePath"};
            break;
        case ast::LiteralKind::none:
            result = unknown_type;
            break;
        }
        break;
    case ast::ExpressionKind::node_reference:
        result = variant_type;
        break;
    case ast::ExpressionKind::identifier: {
        if (expression.value() == "self") {
            result = current_inner_class_ ? Type{TypeKind::object, current_inner_class_->name}
                     : current_script_    ? Type{TypeKind::object, current_script_->script_name}
                                          : Type{TypeKind::object, base_type_};
            break;
        }
        if (expression.value() == "super") {
            const auto* base_script = script_symbols_ && current_script_
                                          ? script_symbols_->base_of(*current_script_)
                                          : nullptr;
            result = base_script ? Type{TypeKind::object, base_script->script_name}
                                 : Type{TypeKind::object, base_type_};
            record_script_dependency(base_script);
            model_.api_resolutions_.emplace(
                &expression,
                ApiResolution{ApiResolutionKind::script_super,
                              base_script ? base_script->native_class_name : "godot::" + base_type_,
                              "", "", result, 0, 0, false, true});
            break;
        }
        if (const auto* symbol = resolve(expression.value())) {
            result = symbol->kind == SymbolKind::function ? Type{TypeKind::builtin, "Callable"}
                                                          : symbol->type;
            model_.referenced_symbols_.emplace(&expression, *symbol);
            if (symbol->kind == SymbolKind::function) {
                bool is_static = false;
                if (const auto found = functions_.find(expression.value());
                    found != functions_.end()) {
                    is_static = found->second->is_static;
                } else if (current_inner_class_) {
                    const auto inner_member =
                        std::find_if(current_inner_class_->members.begin(),
                                     current_inner_class_->members.end(), [&](const auto& member) {
                                         return member.kind == ScriptMemberKind::function &&
                                                member.name == expression.value();
                                     });
                    is_static = inner_member != current_inner_class_->members.end() &&
                                inner_member->is_static;
                } else if (script_symbols_ && current_script_) {
                    const auto* member =
                        script_symbols_->find_member(*current_script_, expression.value());
                    is_static =
                        member && member->kind == ScriptMemberKind::function && member->is_static;
                }
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{is_static ? ApiResolutionKind::script_static_callable
                                                         : ApiResolutionKind::script_callable,
                                               "", "", "", result, 0, 0, false, false});
            }
            if (symbol->kind == SymbolKind::constant) {
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::script_constant, "", "", "",
                                               result, 0, 0, false, true});
            }
            if (symbol->kind == SymbolKind::enum_value) {
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::enum_member, "",
                                                              "", "", result, 0, 0, false, true});
            }
            if (symbol->kind == SymbolKind::field &&
                accessor_fields_.find(expression.value()) != accessor_fields_.end() &&
                current_accessor_fields_.find(expression.value()) ==
                    current_accessor_fields_.end()) {
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_property,
                                                              "", "_gdpp_get_" + expression.value(),
                                                              "_gdpp_set_" + expression.value(),
                                                              result, 0, 0, false, false});
            } else if (symbol->kind == SymbolKind::field &&
                       static_fields_.find(expression.value()) != static_fields_.end()) {
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::script_static_field, "", "", "",
                                               result, 0, 0, false, true});
            }
            if (symbol->kind == SymbolKind::signal) {
                result = {TypeKind::builtin, "Signal"};
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_signal, "",
                                                              "", "", result, 0, 0, false, false});
            }
            if (symbol->kind == SymbolKind::enum_type) {
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_enum_type,
                                                              expression.value(), "", "", result, 0,
                                                              0, false, true});
            }
        } else if (expression.value() == "PI" || expression.value() == "TAU" ||
                   expression.value() == "INF" || expression.value() == "NAN") {
            result = {TypeKind::floating, "float"};
            const auto cpp_name = expression.value() == "PI"    ? "Math_PI"
                                  : expression.value() == "TAU" ? "Math_TAU"
                                  : expression.value() == "INF" ? "Math_INF"
                                                                : "Math_NAN";
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::global_constant, cpp_name, "", "",
                                           result, 0, 0, false, true});
        } else if (const auto* class_constant =
                       api_.find_class_constant(base_type_, expression.value())) {
            result = {TypeKind::integer, "int"};
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::global_constant,
                                                          std::to_string(class_constant->value), "",
                                                          "", result, 0, 0, false, true});
        } else if (const auto* constant = api_.find_global_constant(expression.value())) {
            result = {TypeKind::integer, "int"};
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::global_constant,
                                                          std::to_string(constant->value), "", "",
                                                          result, 0, 0, false, true});
        } else if (api_.has_global_enum(expression.value())) {
            result = {TypeKind::enumeration, expression.value()};
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::global_enum_type, expression.value(),
                                           "", "", result, 0, 0, false, true});
        } else if (const auto* enum_value = api_.find_global_enum_value(expression.value())) {
            result = {TypeKind::integer, "int"};
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::global_enum_value,
                                                          std::to_string(enum_value->value), "", "",
                                                          result, 0, 0, false, true});
        } else if (const auto* inner = find_inner_class(expression.value())) {
            result = {TypeKind::object, inner->name};
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::inner_type_reference, inner->name, "",
                                           "", result, 0, 0, false, true});
        } else if (const auto* autoload = script_symbols_
                                              ? script_symbols_->find_autoload(expression.value())
                                              : nullptr) {
            // Project globals live in the GDScript global namespace and may intentionally use a
            // name that also exists in extension_api.json. Match Godot's project lookup by
            // resolving autoloads before engine singletons and native classes.
            result = {TypeKind::object, autoload->script_name};
            record_script_dependency(autoload);
            model_.api_resolutions_.emplace(
                &expression,
                ApiResolution{ApiResolutionKind::script_autoload, autoload->native_class_name,
                              expression.value(), "", result, 0, 0, false, false});
        } else if (const auto* project_type = script_symbols_
                                                  ? script_symbols_->find_global(expression.value())
                                                  : nullptr) {
            result = {TypeKind::object, project_type->script_name};
            record_script_dependency(project_type);
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::script_type_reference,
                                                          project_type->native_class_name, "", "",
                                                          result, 0, 0, false, true});
        } else if (const auto* external_type =
                       script_symbols_ ? script_symbols_->find_external(expression.value())
                                       : nullptr) {
            result = {TypeKind::object, external_type->name};
            model_.referenced_extension_abis_.insert(external_type->provider_abi);
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::external_type_reference,
                                           external_type->name, "", "", result, 0, 0, false, true});
        } else if (const auto* singleton = api_.find_singleton(expression.value())) {
            result = {TypeKind::object, singleton->type};
            model_.api_resolutions_.emplace(&expression, ApiResolution{ApiResolutionKind::singleton,
                                                                       singleton->type, "", "",
                                                                       result, 0, 0, false, true});
        } else if (script_symbols_) {
            if (api_.find_signal(base_type_, expression.value())) {
                result = {TypeKind::builtin, "Signal"};
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::script_signal, base_type_, "", "",
                                               result, 0, 0, false, false});
            } else if (api_.find_method(base_type_, expression.value())) {
                result = {TypeKind::builtin, "Callable"};
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::script_callable, base_type_, "",
                                               "", result, 0, 0, false, false});
            } else if (const auto* engine_type = api_.find_class(expression.value())) {
                result = type_from_annotation(engine_type->name);
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::type_reference, engine_type->name,
                                               "", "", result, 0, 0, false, true});
            } else if (const auto* property = api_.find_property(base_type_, expression.value())) {
                result = type_from_godot_api(property->type);
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::property, property->owner,
                                               property->getter, property->setter, result, 0, 0,
                                               false, property->direct, property->index});
            }
        } else if (api_.find_signal(base_type_, expression.value())) {
            result = {TypeKind::builtin, "Signal"};
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::script_signal, base_type_, "", "",
                                           result, 0, 0, false, false});
        } else if (api_.find_method(base_type_, expression.value())) {
            result = {TypeKind::builtin, "Callable"};
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::script_callable, base_type_, "", "",
                                           result, 0, 0, false, false});
        } else if (const auto* type = api_.find_class(expression.value())) {
            result = type_from_annotation(type->name);
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::type_reference, type->name, "", "",
                                           result, 0, 0, false, true});
        } else if (const auto* property = api_.find_property(base_type_, expression.value())) {
            result = type_from_godot_api(property->type);
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::property, property->owner,
                                           property->getter, property->setter, result, 0, 0, false,
                                           property->direct, property->index});
        }
        // GDExtensions may register named engine singletons that are intentionally absent from
        // Godot's official extension_api.json (for example GodotSteam's `Steam`). Offline AOT
        // compilation cannot load arbitrary project extensions, so preserve GDScript's global
        // singleton convention and resolve unknown PascalCase globals through Engine at runtime.
        if (model_.api_resolutions_.find(&expression) == model_.api_resolutions_.end() &&
            model_.referenced_symbols_.find(&expression) == model_.referenced_symbols_.end() &&
            !expression.value().empty() &&
            std::isupper(static_cast<unsigned char>(expression.value().front())) != 0) {
            result = variant_type;
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::external_singleton,
                                           expression.value(), "", "", result, 0, 0, false, false});
        }
        if (model_.api_resolutions_.find(&expression) == model_.api_resolutions_.end() &&
            model_.referenced_symbols_.find(&expression) == model_.referenced_symbols_.end()) {
            diagnostics_.error("GDS4122", "unknown identifier '" + expression.value() + "'",
                               expression.span);
        }
        break;
    }
    case ast::ExpressionKind::lambda:
        if (!expression.lambda()) {
            diagnostics_.error("GDS4098", "lambda expression is missing its function body",
                               expression.span);
            result = unknown_type;
        } else {
            analyze_lambda(*expression.lambda());
            result = {TypeKind::builtin, "Callable"};
        }
        break;
    case ast::ExpressionKind::unary: {
        const auto operand = analyze_expression(*expression.operand(0));
        if (operand.is_dynamic()) {
            result = expression.value() == "not" ? Type{TypeKind::boolean, "bool"} : variant_type;
        } else if (expression.value() == "not" && operand.kind == TypeKind::object) {
            result = {TypeKind::boolean, "bool"};
        } else {
            const auto operation = expression.value() == "+"   ? "unary+"
                                   : expression.value() == "-" ? "unary-"
                                                               : expression.value();
            const auto owner = builtin_operator_type(operand);
            const auto* record = api_.find_builtin_operator(owner, operation);
            if (!record) {
                diagnostics_.error("GDS4003",
                                   "operator '" + expression.value() + "' is not defined for " +
                                       operand.display_name(),
                                   expression.span);
                result = unknown_type;
            } else {
                result = type_from_godot_api(record->return_type);
            }
        }
        break;
    }
    case ast::ExpressionKind::binary: {
        const auto left = analyze_expression(*expression.operand(0));
        const auto right = analyze_expression(*expression.operand(1));
        const auto& operation = expression.value();
        if (operation == "is" || operation == "is not") {
            const auto* target = model_.api_resolution_of(*expression.operand(1));
            const bool valid_target = target &&
                                      (target->kind == ApiResolutionKind::type_reference ||
                                       target->kind == ApiResolutionKind::external_type_reference ||
                                       target->kind == ApiResolutionKind::script_type_reference ||
                                       target->kind == ApiResolutionKind::inner_type_reference) &&
                                      right.kind != TypeKind::void_type;
            if (!valid_target) {
                diagnostics_.error("GDS4067", "the right operand of 'is' must be a type",
                                   expression.operand(1)->span);
            }
            const auto* value_resolution = model_.api_resolution_of(*expression.operand(0));
            if (left.kind == TypeKind::void_type ||
                (value_resolution &&
                 (value_resolution->kind == ApiResolutionKind::type_reference ||
                  value_resolution->kind == ApiResolutionKind::external_type_reference ||
                  value_resolution->kind == ApiResolutionKind::script_type_reference))) {
                diagnostics_.error("GDS4068", "the left operand of 'is' must be a value",
                                   expression.operand(0)->span);
            }
            result = {TypeKind::boolean, "bool"};
            break;
        }
        if (operation == "as") {
            const auto* target = model_.api_resolution_of(*expression.operand(1));
            const bool valid_target = target &&
                                      (target->kind == ApiResolutionKind::type_reference ||
                                       target->kind == ApiResolutionKind::external_type_reference ||
                                       target->kind == ApiResolutionKind::script_type_reference ||
                                       target->kind == ApiResolutionKind::inner_type_reference ||
                                       target->kind == ApiResolutionKind::script_enum_type ||
                                       target->kind == ApiResolutionKind::global_enum_type) &&
                                      right.kind != TypeKind::void_type;
            if (!valid_target) {
                diagnostics_.error("GDS4074", "the right operand of 'as' must be a type",
                                   expression.operand(1)->span);
                result = unknown_type;
            } else {
                result = right;
            }
            break;
        }
        if (operation == "and" || operation == "or") {
            // Logical operators use GDScript truthiness for every value category, including
            // typed containers, strings and numeric values.
            result = {TypeKind::boolean, "bool"};
        } else if (left.is_dynamic() || right.is_dynamic()) {
            result = operation == "==" || operation == "!=" || operation == "<" ||
                             operation == "<=" || operation == ">" || operation == ">=" ||
                             operation == "in" || operation == "not in" || operation == "and" ||
                             operation == "or"
                         ? Type{TypeKind::boolean, "bool"}
                         : variant_type;
        } else if ((operation == "==" || operation == "!=") &&
                   ((left.kind == TypeKind::object && right.kind == TypeKind::object) ||
                    (left.kind == TypeKind::object && right.kind == TypeKind::nil) ||
                    (left.kind == TypeKind::nil && right.kind == TypeKind::object))) {
            result = {TypeKind::boolean, "bool"};
        } else {
            const auto left_name = builtin_operator_type(left);
            const auto right_name = builtin_operator_type(right);
            const auto lookup_operation = operation == "not in" ? "in" : operation;
            const auto* record =
                api_.find_builtin_operator(left_name, lookup_operation, right_name);
            if (!record)
                record = api_.find_builtin_operator(left_name, lookup_operation, "Variant");
            if (!record) {
                diagnostics_.error("GDS4005",
                                   "operator '" + operation + "' is not defined for " +
                                       left.display_name() + " and " + right.display_name(),
                                   expression.span);
                result = unknown_type;
            } else {
                result = operation == "not in" ? Type{TypeKind::boolean, "bool"}
                                               : type_from_godot_api(record->return_type);
            }
        }
        break;
    }
    case ast::ExpressionKind::conditional: {
        const auto when_true = analyze_expression(*expression.operand(0));
        (void)analyze_expression(*expression.operand(1));
        const auto when_false = analyze_expression(*expression.operand(2));
        if (when_true == when_false) {
            result = when_true;
        } else if (when_true.is_numeric() && when_false.is_numeric()) {
            result = when_true.kind == TypeKind::floating || when_false.kind == TypeKind::floating
                         ? Type{TypeKind::floating, "float"}
                         : Type{TypeKind::integer, "int"};
        } else if (when_true.kind == TypeKind::nil && when_false.kind == TypeKind::object) {
            result = when_false;
        } else if (when_false.kind == TypeKind::nil && when_true.kind == TypeKind::object) {
            result = when_true;
        } else {
            result = variant_type;
        }
        break;
    }
    case ast::ExpressionKind::call: {
        const auto& callee = *expression.operand(0);
        std::vector<Type> argument_types;
        argument_types.reserve(expression.operand_count() - 1);
        for (std::size_t index = 1; index < expression.operand_count(); ++index)
            argument_types.push_back(analyze_expression(*expression.operand(index)));
        const auto argument_count = expression.operand_count() - 1;
        const auto* language_intrinsic = callee.kind() == ast::ExpressionKind::identifier
                                             ? IntrinsicRegistry::latest().find(callee.value())
                                             : nullptr;
        const auto resolve_method = [&](const GodotMethodRecord* method) {
            if (!method)
                return false;
            if (argument_count < method->required_arguments ||
                (!method->is_vararg && argument_count > method->maximum_arguments)) {
                diagnostics_.error("GDS4011",
                                   "method '" + std::string{method->name} + "' expects " +
                                       std::to_string(method->required_arguments) +
                                       (method->is_vararg ? " or more argument(s)"
                                        : method->required_arguments == method->maximum_arguments
                                            ? " argument(s)"
                                            : " to " + std::to_string(method->maximum_arguments) +
                                                  " argument(s)") +
                                       ", got " + std::to_string(argument_count),
                                   expression.span);
            }
            const auto checked_arguments =
                std::min(argument_count, static_cast<std::size_t>(method->maximum_arguments));
            for (std::size_t index = 0; index < checked_arguments; ++index) {
                if (const auto* argument = api_.argument(*method, index)) {
                    require_assignable(type_from_godot_api(argument->type), argument_types[index],
                                       expression.operand(index + 1)->span,
                                       "argument " + std::to_string(index + 1) + " of '" +
                                           method->name + "'");
                }
            }
            result = type_from_godot_api(method->return_type);
            model_.api_resolutions_.emplace(
                &callee, ApiResolution{ApiResolutionKind::method, method->owner, "", "", result,
                                       method->required_arguments, method->maximum_arguments,
                                       method->is_vararg, false});
            return true;
        };
        const auto resolve_constructor = [&](const std::string& name) {
            const auto* type_record = api_.find_class(name);
            if (!type_record || !type_record->builtin)
                return false;
            bool has_arity = false;
            for (std::size_t occurrence = 0;; ++occurrence) {
                const auto* constructor = api_.find_constructor(name, argument_count, occurrence);
                if (!constructor)
                    break;
                has_arity = true;
                bool compatible = true;
                for (std::size_t index = 0; index < argument_count; ++index) {
                    const auto* argument = api_.argument(*constructor, index);
                    if (!argument) {
                        compatible = false;
                        break;
                    }
                    const auto target = type_from_godot_api(argument->type);
                    const auto& source = argument_types[index];
                    const bool inherited = target.kind == TypeKind::object &&
                                           source.kind == TypeKind::object &&
                                           api_.inherits(source.name, target.name);
                    const bool packed_color = target.kind == TypeKind::builtin &&
                                              target.name == "Color" &&
                                              source.kind == TypeKind::integer;
                    const bool object_rid =
                        target.kind == TypeKind::builtin && target.name == "RID" &&
                        source.kind == TypeKind::object && api_.find_method(source.name, "get_rid");
                    if (!is_assignable(target, source) && !inherited && !packed_color &&
                        !object_rid) {
                        compatible = false;
                        break;
                    }
                }
                if (!compatible)
                    continue;
                result = type_from_annotation(name);
                model_.expression_types_[&callee] = result;
                model_.api_resolutions_.emplace(
                    &callee, ApiResolution{ApiResolutionKind::constructor, name, "", "", result,
                                           static_cast<std::uint16_t>(argument_count),
                                           static_cast<std::uint16_t>(argument_count), false, true,
                                           static_cast<std::int64_t>(occurrence)});
                return true;
            }
            diagnostics_.error(has_arity ? "GDS4013" : "GDS4014",
                               has_arity ? "no matching constructor for '" + name + "'"
                                         : "constructor for '" + name + "' does not accept " +
                                               std::to_string(argument_count) + " argument(s)",
                               expression.span);
            return true;
        };
        const auto resolve_utility = [&](const GodotUtilityFunctionRecord* function) {
            if (!function)
                return false;
            if (argument_count < function->required_arguments ||
                (!function->is_vararg && argument_count > function->maximum_arguments)) {
                diagnostics_.error(
                    "GDS4073",
                    "utility function '" + std::string{function->name} + "' expects " +
                        std::to_string(function->required_arguments) +
                        (function->is_vararg ? " or more argument(s)"
                         : function->required_arguments == function->maximum_arguments
                             ? " argument(s)"
                             : " to " + std::to_string(function->maximum_arguments) +
                                   " argument(s)") +
                        ", got " + std::to_string(argument_count),
                    expression.span);
            }
            const auto checked =
                std::min(argument_count, static_cast<std::size_t>(function->maximum_arguments));
            for (std::size_t index = 0; index < checked; ++index) {
                if (const auto* argument = api_.argument(*function, index)) {
                    require_assignable(type_from_godot_api(argument->type), argument_types[index],
                                       expression.operand(index + 1)->span,
                                       "argument " + std::to_string(index + 1) + " of '" +
                                           function->name + "'");
                }
            }
            result = type_from_godot_api(function->return_type);
            model_.api_resolutions_.emplace(
                &callee, ApiResolution{ApiResolutionKind::utility_function, function->name, "", "",
                                       result, function->required_arguments,
                                       function->maximum_arguments, function->is_vararg, true});
            return true;
        };
        if (language_intrinsic && (language_intrinsic->kind == IntrinsicKind::preload ||
                                   language_intrinsic->kind == IntrinsicKind::load)) {
            const bool is_preload = language_intrinsic->kind == IntrinsicKind::preload;
            const bool literal_path =
                argument_count == 1 &&
                expression.operand(1)->kind() == ast::ExpressionKind::literal &&
                expression.operand(1)->literal_kind() == ast::LiteralKind::string;
            if (argument_count != 1 || (is_preload && !literal_path)) {
                diagnostics_.error("GDS4060",
                                   callee.value() +
                                       (is_preload
                                            ? " requires exactly one literal project resource path"
                                            : " requires exactly one resource path"),
                                   expression.span);
                break;
            }
            if (!literal_path) {
                require_assignable({TypeKind::string, "String"}, argument_types.front(),
                                   expression.operand(1)->span, "load resource path");
                result = variant_type;
                ApiResolution resolution{
                    ApiResolutionKind::intrinsic, "load", "", "", result, 1, 1, false, true};
                resolution.intrinsic = IntrinsicKind::load;
                model_.api_resolutions_.emplace(&callee, std::move(resolution));
                break;
            }
            ApiResolution intrinsic_resolution{ApiResolutionKind::intrinsic,
                                               std::string{language_intrinsic->name},
                                               "",
                                               "",
                                               variant_type,
                                               1,
                                               1,
                                               false,
                                               true};
            intrinsic_resolution.intrinsic = language_intrinsic->kind;
            model_.api_resolutions_.insert_or_assign(&callee, std::move(intrinsic_resolution));
            const auto& resource_path = expression.operand(1)->value();
            const auto resolved_resource_path =
                script_symbols_
                    ? script_symbols_->resolve_resource_path(current_script_path_, resource_path)
                    : std::optional<std::string>{};
            const auto& effective_resource_path =
                resolved_resource_path ? *resolved_resource_path : resource_path;
            const auto* target =
                script_symbols_ ? script_symbols_->resolve_path(current_script_path_, resource_path)
                                : nullptr;
            if (target) {
                record_script_dependency(target);
                result = {TypeKind::script_resource, target->path};
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_resource,
                                                              target->native_class_name, "", "",
                                                              result, 1, 1, false, true});
            } else if ((effective_resource_path.size() >= 5 &&
                        effective_resource_path.compare(effective_resource_path.size() - 5, 5,
                                                        ".tscn") == 0) ||
                       (effective_resource_path.size() >= 4 &&
                        effective_resource_path.compare(effective_resource_path.size() - 4, 4,
                                                        ".scn") == 0)) {
                result = {TypeKind::object, "PackedScene"};
                auto& resolution = model_.api_resolutions_.at(&callee);
                resolution.type = result;
            } else if (effective_resource_path.size() >= 3 &&
                       effective_resource_path.compare(effective_resource_path.size() - 3, 3,
                                                       ".gd") == 0) {
                diagnostics_.error("GDS4061",
                                   "project script '" + resource_path + "' was not found for " +
                                       callee.value(),
                                   expression.span);
            } else if (resolved_resource_path ||
                       (resource_path.rfind("res://", 0) == 0 && resource_path.size() > 6)) {
                result = variant_type;
                model_.api_resolutions_.at(&callee).type = result;
            } else {
                diagnostics_.error("GDS4061",
                                   "invalid project resource path '" + resource_path + "' for " +
                                       callee.value(),
                                   expression.span);
            }
            break;
        }
        if (callee.kind() == ast::ExpressionKind::identifier) {
            if (callee.value() == "super") {
                const auto* base_script = script_symbols_ && current_script_
                                              ? script_symbols_->base_of(*current_script_)
                                              : nullptr;
                const auto* member =
                    base_script ? script_symbols_->find_member(*base_script, current_function_name_)
                                : nullptr;
                const auto* method =
                    member ? nullptr : api_.find_method(base_type_, current_function_name_);
                if (member && member->kind == ScriptMemberKind::function) {
                    validate_script_call(*member, argument_types, expression.span);
                    result = member->type;
                } else if (method) {
                    (void)resolve_method(method);
                } else {
                    diagnostics_.error("GDS4122",
                                       "super call has no base implementation for '" +
                                           current_function_name_ + "'",
                                       expression.span);
                    result = unknown_type;
                }
                model_.api_resolutions_[&callee] = ApiResolution{
                    ApiResolutionKind::script_super,
                    base_script ? base_script->native_class_name : "godot::" + base_type_,
                    method ? method->owner : "",
                    current_function_name_,
                    result,
                    member   ? static_cast<std::uint16_t>(member->required_arguments)
                    : method ? method->required_arguments
                             : std::uint16_t{0},
                    member   ? static_cast<std::uint16_t>(member->parameters.size())
                    : method ? method->maximum_arguments
                             : std::uint16_t{0},
                    method && method->is_vararg,
                    true};
            } else if (language_intrinsic && language_intrinsic->kind == IntrinsicKind::range) {
                if (argument_count < language_intrinsic->minimum_arguments ||
                    argument_count > language_intrinsic->maximum_arguments) {
                    diagnostics_.error("GDS4075", "range expects 1 to 3 arguments",
                                       expression.span);
                }
                for (std::size_t index = 0; index < argument_types.size(); ++index) {
                    require_assignable({TypeKind::integer, "int"}, argument_types[index],
                                       expression.operand(index + 1)->span,
                                       "range argument " + std::to_string(index + 1));
                }
                result = {TypeKind::array, "Array[int]"};
                ApiResolution resolution{
                    ApiResolutionKind::intrinsic, "range", "", "", result, 1, 3, false, true};
                resolution.intrinsic = IntrinsicKind::range;
                model_.api_resolutions_.emplace(&callee, std::move(resolution));
            } else if (language_intrinsic && language_intrinsic->kind == IntrinsicKind::length) {
                if (argument_count != language_intrinsic->minimum_arguments)
                    diagnostics_.error("GDS4076", "len expects exactly 1 argument",
                                       expression.span);
                result = {TypeKind::integer, "int"};
                ApiResolution resolution{
                    ApiResolutionKind::intrinsic, "len", "", "", result, 1, 1, false, true};
                resolution.intrinsic = IntrinsicKind::length;
                model_.api_resolutions_.emplace(&callee, std::move(resolution));
            } else if (resolve_utility(api_.find_utility_function(callee.value()))) {
            } else if (const auto* symbol = resolve(callee.value())) {
                result = symbol->type;
                model_.referenced_symbols_.emplace(&callee, *symbol);
                if (symbol->kind == SymbolKind::function && script_symbols_ && current_script_) {
                    if (const auto* member =
                            script_symbols_->find_member(*current_script_, callee.value())) {
                        validate_script_call(*member, argument_types, expression.span);
                        result = member->type;
                        if (script_symbols_->requires_dynamic_dispatch(*current_script_,
                                                                       callee.value())) {
                            model_.api_resolutions_.emplace(
                                &callee, ApiResolution{ApiResolutionKind::dynamic_method, "", "",
                                                       "", result, 0, 0, true, false});
                        }
                    }
                } else if (symbol->kind != SymbolKind::function) {
                    diagnostics_.error(
                        "GDS4070",
                        "value '" + callee.value() +
                            "' is not directly callable; Callable values must use .call(...)",
                        expression.span);
                    result = unknown_type;
                }
            } else if (!resolve_method(api_.find_method(base_type_, callee.value()))) {
                if (!resolve_constructor(callee.value())) {
                    result = analyze_expression(callee);
                    if (result.kind == TypeKind::unknown) {
                        diagnostics_.error("GDS4071",
                                           "unknown function or callable '" + callee.value() + "'",
                                           expression.span);
                    }
                }
            }
        } else if (callee.kind() == ast::ExpressionKind::member) {
            const auto object_type = analyze_expression(*callee.operand(0));
            const auto* object_resolution = model_.api_resolution_of(*callee.operand(0));
            const bool called_on_super =
                object_resolution && object_resolution->kind == ApiResolutionKind::script_super;
            if (object_resolution &&
                object_resolution->kind == ApiResolutionKind::external_type_reference) {
                if (callee.value() == "new") {
                    if (argument_count != 0) {
                        diagnostics_.error(
                            "GDS4063",
                            "runtime GDExtension new() does not accept constructor arguments",
                            expression.span);
                    }
                    result = object_type;
                    model_.api_resolutions_.emplace(
                        &callee,
                        ApiResolution{ApiResolutionKind::external_constructor,
                                      object_resolution->owner, "", "", result, 0, 0, false, true});
                    break;
                }
                const auto* external_owner =
                    script_symbols_ ? script_symbols_->find_external(object_resolution->owner)
                                    : nullptr;
                const auto* member =
                    external_owner
                        ? script_symbols_->find_external_member(*external_owner, callee.value())
                        : nullptr;
                if (!member || member->kind != ScriptMemberKind::function || !member->is_static) {
                    diagnostics_.error("GDS4056",
                                       "external type '" + object_resolution->owner +
                                           "' has no declared static method '" + callee.value() +
                                           "'",
                                       expression.span);
                    result = unknown_type;
                    break;
                }
                validate_script_call(*member, argument_types, expression.span);
                result = member->type;
                model_.api_resolutions_.emplace(
                    &callee, ApiResolution{ApiResolutionKind::external_static_method,
                                           object_resolution->owner, "", "", result,
                                           static_cast<std::uint16_t>(member->required_arguments),
                                           static_cast<std::uint16_t>(member->parameters.size()),
                                           member->is_vararg, true});
                break;
            }
            if (object_type.kind == TypeKind::script_resource) {
                const auto* target =
                    script_symbols_ ? script_symbols_->find_path(object_type.name) : nullptr;
                if (!target || callee.value() != "new") {
                    diagnostics_.error("GDS4062", "script resources only expose new() in AOT code",
                                       expression.span);
                    break;
                }
                if (target->is_abstract) {
                    diagnostics_.error("GDS4111",
                                       "cannot instantiate abstract script class '" +
                                           target->script_name + "'",
                                       expression.span);
                }
                if (const auto* initializer = script_symbols_->find_member(*target, "_init")) {
                    validate_script_call(*initializer, argument_types, expression.span);
                } else if (argument_count != 0) {
                    diagnostics_.error("GDS4063",
                                       "script new() received arguments but target has no _init",
                                       expression.span);
                }
                result = {TypeKind::object, target->script_name};
                model_.api_resolutions_.emplace(&callee,
                                                ApiResolution{ApiResolutionKind::script_constructor,
                                                              target->native_class_name, "", "",
                                                              result, 0, 0, false, true});
                break;
            }
            if (const auto* inner = object_type.kind == TypeKind::object
                                        ? find_inner_class(object_type.name)
                                        : nullptr) {
                const bool called_on_type =
                    object_resolution &&
                    object_resolution->kind == ApiResolutionKind::inner_type_reference;
                if (called_on_type && callee.value() == "new") {
                    const auto initializer = std::find_if(
                        inner->members.begin(), inner->members.end(), [](const auto& member) {
                            return member.kind == ScriptMemberKind::function &&
                                   member.name == "_init";
                        });
                    if (initializer != inner->members.end()) {
                        validate_script_call(*initializer, argument_types, expression.span);
                    } else if (argument_count != 0) {
                        diagnostics_.error(
                            "GDS4063",
                            "internal class new() received arguments but target has no _init",
                            expression.span);
                    }
                    result = {TypeKind::object, inner->name};
                    model_.api_resolutions_.emplace(
                        &callee, ApiResolution{ApiResolutionKind::inner_constructor, inner->name,
                                               "", "", result, 0, 0, false, true});
                    break;
                }
                const auto* member = [&]() -> const ScriptMemberSymbol* {
                    const auto found = std::find_if(
                        inner->members.begin(), inner->members.end(),
                        [&](const auto& candidate) { return candidate.name == callee.value(); });
                    return found == inner->members.end() ? nullptr : &*found;
                }();
                if (!member || member->kind != ScriptMemberKind::function) {
                    diagnostics_.error("GDS4055",
                                       "internal class '" + inner->name + "' has no method '" +
                                           callee.value() + "'",
                                       expression.span);
                    result = unknown_type;
                    break;
                }
                if (called_on_type != member->is_static) {
                    diagnostics_.error("GDS4056",
                                       "invalid static/instance call to internal class method '" +
                                           callee.value() + "'",
                                       expression.span);
                }
                validate_script_call(*member, argument_types, expression.span);
                result = member->type;
                break;
            }
            const ScriptClassSymbol* script_owner = nullptr;
            if (script_symbols_ && object_type.kind == TypeKind::object) {
                script_owner =
                    called_on_super && current_script_ ? script_symbols_->base_of(*current_script_)
                    : object_resolution &&
                            (object_resolution->kind == ApiResolutionKind::script_autoload ||
                             object_resolution->kind == ApiResolutionKind::script_type_reference)
                        ? script_symbols_->find_native_class(object_resolution->owner)
                        : script_symbols_->find_class(object_type.name);
                if (!script_owner && callee.operand(0)->kind() == ast::ExpressionKind::identifier &&
                    callee.operand(0)->value() == "self") {
                    script_owner = current_script_;
                }
            }
            if (script_owner) {
                record_script_dependency(script_owner);
                const bool called_on_type =
                    object_resolution &&
                    object_resolution->kind == ApiResolutionKind::script_type_reference;
                if (called_on_type && callee.value() == "new") {
                    if (script_owner->is_abstract) {
                        diagnostics_.error("GDS4111",
                                           "cannot instantiate abstract script class '" +
                                               script_owner->script_name + "'",
                                           expression.span);
                    }
                    if (const auto* initializer =
                            script_symbols_->find_member(*script_owner, "_init")) {
                        validate_script_call(*initializer, argument_types, expression.span);
                    } else if (argument_count != 0) {
                        diagnostics_.error(
                            "GDS4063", "script new() received arguments but target has no _init",
                            expression.span);
                    }
                    result = {TypeKind::object, script_owner->script_name};
                    model_.api_resolutions_.emplace(
                        &callee, ApiResolution{ApiResolutionKind::script_constructor,
                                               script_owner->native_class_name, "", "", result, 0,
                                               0, false, true});
                    break;
                }
                if (!called_on_type && callee.value() == "free") {
                    if (argument_count != 0)
                        diagnostics_.error("GDS4064", "free() does not accept arguments",
                                           expression.span);
                    result = void_type;
                    model_.api_resolutions_.emplace(
                        &callee, ApiResolution{ApiResolutionKind::script_free, "", "", "", result,
                                               0, 0, false, true});
                    break;
                }
                const auto* member = script_symbols_->find_member(*script_owner, callee.value());
                if (!member) {
                    if (!called_on_type) {
                        const auto* method =
                            api_.find_method(script_owner->godot_base_type, callee.value());
                        if (resolve_method(method)) {
                            if (called_on_super) {
                                model_.api_resolutions_[&callee] = ApiResolution{
                                    ApiResolutionKind::script_super,
                                    object_resolution->owner,
                                    method ? method->owner : "",
                                    "",
                                    result,
                                    method ? method->required_arguments : std::uint16_t{0},
                                    method ? method->maximum_arguments : std::uint16_t{0},
                                    method && method->is_vararg,
                                    true};
                            }
                            break;
                        }
                        if (GodotApi::for_version(latest_godot_version)
                                .find_method(script_owner->godot_base_type, callee.value())) {
                            diagnostics_.error("GDS4016",
                                               "method '" + callee.value() +
                                                   "' is not available on Godot type '" +
                                                   script_owner->godot_base_type +
                                                   "' for the selected target version",
                                               expression.span);
                            break;
                        }
                    }
                    if (!called_on_type && !called_on_super) {
                        result = variant_type;
                        model_.api_resolutions_.emplace(
                            &callee, ApiResolution{ApiResolutionKind::dynamic_method, "", "", "",
                                                   result, 0, 0, true, false});
                        break;
                    }
                    diagnostics_.error("GDS4055",
                                       "script type '" + script_owner->script_name +
                                           "' has no member '" + callee.value() + "'",
                                       expression.span);
                    break;
                }
                if (called_on_type && !member->is_static) {
                    diagnostics_.error("GDS4056",
                                       "instance member '" + callee.value() +
                                           "' cannot be called on a script type",
                                       expression.span);
                } else if (!called_on_type && member->is_static) {
                    diagnostics_.error("GDS4057",
                                       "static method '" + callee.value() +
                                           "' must be called on its script type",
                                       expression.span);
                }
                validate_script_call(*member, argument_types, expression.span);
                result = member->type;
                if (called_on_super) {
                    model_.api_resolutions_.emplace(
                        &callee,
                        ApiResolution{
                            ApiResolutionKind::script_super, object_resolution->owner, "", "",
                            result, static_cast<std::uint16_t>(member->required_arguments),
                            static_cast<std::uint16_t>(member->parameters.size()), false, true});
                } else if (!called_on_type && script_symbols_->requires_dynamic_dispatch(
                                                  *script_owner, callee.value())) {
                    model_.api_resolutions_.emplace(
                        &callee, ApiResolution{ApiResolutionKind::dynamic_method, "", "", "",
                                               result, 0, 0, true, false});
                }
                break;
            }
            const auto* external_owner = object_type.kind == TypeKind::object && script_symbols_
                                             ? script_symbols_->find_external(object_type.name)
                                             : nullptr;
            if (external_owner) {
                if (const auto* member =
                        script_symbols_->find_external_member(*external_owner, callee.value())) {
                    if (member->kind != ScriptMemberKind::function) {
                        diagnostics_.error(
                            "GDS4070", "external member '" + callee.value() + "' is not callable",
                            expression.span);
                        result = unknown_type;
                    } else if (member->is_static) {
                        diagnostics_.error("GDS4057",
                                           "static external method '" + callee.value() +
                                               "' must be called on its type",
                                           expression.span);
                        result = unknown_type;
                    } else {
                        validate_script_call(*member, argument_types, expression.span);
                        result = member->type;
                        model_.api_resolutions_.emplace(
                            &callee,
                            ApiResolution{ApiResolutionKind::dynamic_method, external_owner->name,
                                          "", "", result,
                                          static_cast<std::uint16_t>(member->required_arguments),
                                          static_cast<std::uint16_t>(member->parameters.size()),
                                          member->is_vararg, false});
                    }
                    break;
                }
                if (external_owner->members_complete) {
                    diagnostics_.error("GDS4112",
                                       "external type '" + external_owner->name +
                                           "' has no declared method '" + callee.value() + "'",
                                       expression.span);
                    result = unknown_type;
                    break;
                }
            }
            if (object_type.is_dynamic()) {
                result = variant_type;
                model_.api_resolutions_.emplace(&callee,
                                                ApiResolution{ApiResolutionKind::dynamic_method, "",
                                                              "", "", result, 0, 0, true, false});
                break;
            }
            std::string owner;
            if (object_type.kind == TypeKind::array)
                owner = "Array";
            else if (object_type.kind == TypeKind::dictionary)
                owner = "Dictionary";
            else if (object_type.kind == TypeKind::string)
                owner = "String";
            else if (object_type.kind == TypeKind::string_name)
                owner = "StringName";
            else if (object_type.kind == TypeKind::builtin || object_type.kind == TypeKind::object)
                owner = object_type.name;
            const auto* method = api_.find_method(owner, callee.value());
            const auto* latest_method =
                GodotApi::for_version(latest_godot_version).find_method(owner, callee.value());
            const bool called_on_type =
                object_resolution && object_resolution->kind == ApiResolutionKind::type_reference;
            if (called_on_type && object_type.kind == TypeKind::object && callee.value() == "new") {
                if (argument_count != 0) {
                    diagnostics_.error("GDS4077", "Godot object new() does not accept arguments",
                                       expression.span);
                }
                result = object_type;
                model_.api_resolutions_.emplace(&callee,
                                                ApiResolution{ApiResolutionKind::constructor, owner,
                                                              "", "", result, 0, 0, false, true});
                break;
            }
            const bool dynamically_typed_object =
                object_type.kind == TypeKind::object &&
                (api_.find_class(object_type.name) ||
                 (script_symbols_ && script_symbols_->find_external(object_type.name)));
            if (!method && !latest_method && dynamically_typed_object && !called_on_type) {
                result = variant_type;
                model_.api_resolutions_.emplace(&callee,
                                                ApiResolution{ApiResolutionKind::dynamic_method, "",
                                                              "", "", result, 0, 0, true, false});
                break;
            }
            if (!method && !owner.empty() && !object_type.is_dynamic()) {
                diagnostics_.error("GDS4016",
                                   "method '" + callee.value() +
                                       "' is not available on Godot type '" + owner +
                                       "' for the selected target version",
                                   expression.span);
            }
            if (method && called_on_type && !method->is_static) {
                diagnostics_.error("GDS4015",
                                   "instance method '" + callee.value() +
                                       "' cannot be called on a Godot type",
                                   expression.span);
            }
            if (resolve_method(method)) {
                if (called_on_super) {
                    model_.api_resolutions_[&callee] =
                        ApiResolution{ApiResolutionKind::script_super,
                                      object_resolution->owner,
                                      method ? method->owner : "",
                                      "",
                                      result,
                                      method ? method->required_arguments : std::uint16_t{0},
                                      method ? method->maximum_arguments : std::uint16_t{0},
                                      method && method->is_vararg,
                                      true};
                }
            } else {
                result = analyze_expression(callee);
            }
        } else {
            result = analyze_expression(callee);
            diagnostics_.error(
                "GDS4072",
                "expression is not directly callable; invoke a Callable through .call(...)",
                expression.span);
            result = unknown_type;
        }
        break;
    }
    case ast::ExpressionKind::member: {
        const auto object_type = analyze_expression(*expression.operand(0));
        if (object_type.kind == TypeKind::enumeration) {
            if (const auto* global =
                    api_.find_global_enum_value(object_type.name, expression.value())) {
                result = object_type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::global_enum_value,
                                                              std::to_string(global->value), "", "",
                                                              result, 0, 0, false, true});
                break;
            }
            if (const auto separator = object_type.name.rfind('.');
                separator != std::string::npos) {
                const auto owner = object_type.name.substr(0, separator);
                const auto enum_name = object_type.name.substr(separator + 1);
                if (const auto* class_value =
                        api_.find_class_enum_value(owner, enum_name, expression.value())) {
                    result = object_type;
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_enum_value,
                                                   std::to_string(class_value->value), "", "",
                                                   result, 0, 0, false, true});
                    break;
                }
            }
            const auto enumeration = enum_members_.find(object_type.name);
            const auto project_enum = find_project_enum(script_symbols_, object_type.name);
            const auto external_enum = find_external_enum(script_symbols_, object_type.name);
            if (external_enum.enumeration) {
                const auto found =
                    std::find_if(external_enum.enumeration->entries.begin(),
                                 external_enum.enumeration->entries.end(), [&](const auto& entry) {
                                     return entry.name == expression.value();
                                 });
                if (found == external_enum.enumeration->entries.end()) {
                    diagnostics_.error("GDS4041",
                                       "enum '" + object_type.name + "' has no member '" +
                                           expression.value() + "'",
                                       expression.span);
                    result = unknown_type;
                } else {
                    result = object_type;
                    model_.referenced_extension_abis_.insert(external_enum.owner->provider_abi);
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_enum_value,
                                                   std::to_string(found->value), "", "", result, 0,
                                                   0, false, true});
                }
            } else if (enumeration == enum_members_.end() && !project_enum.enumeration) {
                diagnostics_.error("GDS4041",
                                   "enum '" + object_type.name + "' has no member '" +
                                       expression.value() + "'",
                                   expression.span);
                result = unknown_type;
            } else if (enumeration != enum_members_.end()) {
                const auto member = enumeration->second.find(expression.value());
                if (member == enumeration->second.end()) {
                    diagnostics_.error("GDS4041",
                                       "enum '" + object_type.name + "' has no member '" +
                                           expression.value() + "'",
                                       expression.span);
                    result = unknown_type;
                } else {
                    result = object_type;
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::enum_member, object_type.name,
                                                   "", "", result, 0, 0, false, true});
                }
            } else {
                const auto found =
                    std::find_if(project_enum.enumeration->entries.begin(),
                                 project_enum.enumeration->entries.end(), [&](const auto& entry) {
                                     return entry.name == expression.value();
                                 });
                if (found == project_enum.enumeration->entries.end()) {
                    diagnostics_.error("GDS4041",
                                       "enum '" + object_type.name + "' has no member '" +
                                           expression.value() + "'",
                                       expression.span);
                    result = unknown_type;
                } else {
                    result = object_type;
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::enum_member,
                                                   project_enum.owner->native_class_name +
                                                       "::" + project_enum.enumeration->name,
                                                   "", "", result, 0, 0, false, true});
                }
            }
            record_script_dependency(project_enum.owner);
            break;
        }
        const auto* object_resolution = model_.api_resolution_of(*expression.operand(0));
        if (object_resolution && object_resolution->kind == ApiResolutionKind::script_super) {
            diagnostics_.error("GDS4089", "super members can only be used as method calls",
                               expression.span);
            result = unknown_type;
            break;
        }
        if (object_type.is_dynamic()) {
            result = variant_type;
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::dynamic_property, "",
                                                          "", "", result, 0, 0, false, false});
            break;
        }
        if (const auto* inner = object_type.kind == TypeKind::object
                                    ? find_inner_class(object_type.name)
                                    : nullptr) {
            const bool accessed_on_type =
                object_resolution &&
                object_resolution->kind == ApiResolutionKind::inner_type_reference;
            const auto found =
                std::find_if(inner->members.begin(), inner->members.end(),
                             [&](const auto& member) { return member.name == expression.value(); });
            if (found == inner->members.end()) {
                diagnostics_.error("GDS4055",
                                   "internal class '" + inner->name + "' has no member '" +
                                       expression.value() + "'",
                                   expression.span);
                result = unknown_type;
                break;
            }
            if (found->kind == ScriptMemberKind::function) {
                if (accessed_on_type || found->is_static) {
                    diagnostics_.error("GDS4096",
                                       "only instance internal methods can be Callable values",
                                       expression.span);
                    result = unknown_type;
                } else {
                    result = {TypeKind::builtin, "Callable"};
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::script_callable, inner->name,
                                                   "", "", result, 0, 0, false, false});
                }
                break;
            }
            if (found->kind == ScriptMemberKind::enum_value) {
                result = found->type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::enum_member,
                                                              inner->native_class_name, "", "",
                                                              result, 0, 0, false, true});
                break;
            }
            if (found->kind == ScriptMemberKind::constant) {
                result = found->type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_constant,
                                                              inner->native_class_name, "", "",
                                                              result, 0, 0, false, true});
                break;
            }
            if (found->kind != ScriptMemberKind::field || accessed_on_type) {
                diagnostics_.error("GDS4058", "invalid internal class member access",
                                   expression.span);
                result = unknown_type;
                break;
            }
            result = found->type;
            model_.api_resolutions_.emplace(
                &expression,
                ApiResolution{ApiResolutionKind::script_property, inner->name,
                              "_gdpp_get_" + expression.value(), "_gdpp_set_" + expression.value(),
                              result, 0, 0, false, false});
            break;
        }
        const ScriptClassSymbol* script_owner = nullptr;
        if (script_symbols_ && object_type.kind == TypeKind::object) {
            script_owner =
                object_resolution &&
                        (object_resolution->kind == ApiResolutionKind::script_autoload ||
                         object_resolution->kind == ApiResolutionKind::script_type_reference)
                    ? script_symbols_->find_native_class(object_resolution->owner)
                    : script_symbols_->find_class(object_type.name);
            if (!script_owner && expression.operand(0)->kind() == ast::ExpressionKind::identifier &&
                expression.operand(0)->value() == "self") {
                script_owner = current_script_;
            }
        }
        if (script_owner) {
            record_script_dependency(script_owner);
            const bool accessed_on_type =
                object_resolution &&
                object_resolution->kind == ApiResolutionKind::script_type_reference;
            if (const auto* enumeration =
                    script_symbols_->find_enum(*script_owner, expression.value())) {
                result = {TypeKind::enumeration,
                          script_owner->script_name + "." + enumeration->name};
                model_.api_resolutions_.emplace(
                    &expression,
                    ApiResolution{ApiResolutionKind::script_enum_type,
                                  script_owner->native_class_name + "::" + enumeration->name, "",
                                  "", result, 0, 0, false, true});
                break;
            }
            const auto* member = script_symbols_->find_member(*script_owner, expression.value());
            if (!member) {
                if (accessed_on_type &&
                    api_.has_class_enum(script_owner->godot_base_type, expression.value())) {
                    result = {TypeKind::enumeration,
                              script_owner->godot_base_type + "." + expression.value()};
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_enum_type,
                                                   "godot::" + script_owner->godot_base_type +
                                                       "::" + expression.value(),
                                                   "", "", result, 0, 0, false, true});
                    break;
                }
                if (const auto* constant = api_.find_class_constant(script_owner->godot_base_type,
                                                                    expression.value())) {
                    result = {TypeKind::integer, "int"};
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_constant,
                                                   std::to_string(constant->value), "", "", result,
                                                   0, 0, false, true});
                    break;
                }
                if (!accessed_on_type) {
                    if (api_.find_signal(script_owner->godot_base_type, expression.value())) {
                        result = {TypeKind::builtin, "Signal"};
                        model_.api_resolutions_.emplace(
                            &expression, ApiResolution{ApiResolutionKind::script_signal,
                                                       script_owner->godot_base_type, "", "",
                                                       result, 0, 0, false, false});
                        break;
                    }
                    if (const auto* property =
                            api_.find_property(script_owner->godot_base_type, expression.value())) {
                        result = type_from_godot_api(property->type);
                        model_.api_resolutions_.emplace(
                            &expression,
                            ApiResolution{ApiResolutionKind::property, property->owner,
                                          property->getter, property->setter, result, 0, 0, false,
                                          property->direct, property->index});
                        break;
                    }
                }
                if (!accessed_on_type) {
                    result = variant_type;
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::dynamic_property, "", "", "",
                                                   result, 0, 0, false, false});
                    break;
                }
                diagnostics_.error("GDS4055",
                                   "script type '" + script_owner->script_name +
                                       "' has no member '" + expression.value() + "'",
                                   expression.span);
                result = unknown_type;
                break;
            }
            if (accessed_on_type && member->kind == ScriptMemberKind::constant) {
                result = member->type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_constant,
                                                              script_owner->native_class_name, "",
                                                              "", result, 0, 0, false, true});
                break;
            }
            if (member->kind == ScriptMemberKind::enum_value) {
                result = member->type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::enum_member,
                                                              script_owner->native_class_name, "",
                                                              "", result, 0, 0, false, true});
                break;
            }
            if (member->kind == ScriptMemberKind::constant) {
                // Godot permits class constants through script instances and Autoload names.
                // Lower them to the native class scope so no singleton lookup is required.
                result = member->type;
                model_.api_resolutions_.emplace(&expression,
                                                ApiResolution{ApiResolutionKind::script_constant,
                                                              script_owner->native_class_name, "",
                                                              "", result, 0, 0, false, true});
                break;
            }
            if (accessed_on_type && member->kind == ScriptMemberKind::field && !member->is_static) {
                diagnostics_.error("GDS4058",
                                   "instance field '" + expression.value() +
                                       "' cannot be accessed on a script type",
                                   expression.span);
                result = unknown_type;
                break;
            }
            if (member->kind == ScriptMemberKind::signal) {
                if (accessed_on_type) {
                    diagnostics_.error("GDS4094", "signals cannot be accessed on a script type",
                                       expression.span);
                    result = unknown_type;
                } else {
                    result = {TypeKind::builtin, "Signal"};
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::script_signal,
                                                   script_owner->native_class_name, "", "", result,
                                                   0, 0, false, false});
                }
                break;
            }
            if (member->kind == ScriptMemberKind::function) {
                if (accessed_on_type || member->is_static) {
                    diagnostics_.error(
                        "GDS4096", "only instance script methods can be used as Callable values",
                        expression.span);
                    result = unknown_type;
                } else {
                    result = {TypeKind::builtin, "Callable"};
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::script_callable,
                                                   script_owner->native_class_name, "", "", result,
                                                   0, 0, false, false});
                }
                break;
            }
            if (member->kind != ScriptMemberKind::field) {
                diagnostics_.error("GDS4058",
                                   "script member '" + expression.value() +
                                       "' is not an instance field",
                                   expression.span);
                result = unknown_type;
                break;
            }
            result = member->type;
            model_.api_resolutions_.emplace(
                &expression,
                ApiResolution{ApiResolutionKind::script_property, script_owner->native_class_name,
                              "_gdpp_get_" + expression.value(), "_gdpp_set_" + expression.value(),
                              result, 0, 0, false, false});
            break;
        }
        if (const auto* external_owner = object_type.kind == TypeKind::object && script_symbols_
                                             ? script_symbols_->find_external(object_type.name)
                                             : nullptr) {
            const bool accessed_on_type =
                object_resolution &&
                object_resolution->kind == ApiResolutionKind::external_type_reference;
            if (accessed_on_type) {
                if (const auto* enumeration =
                        script_symbols_->find_external_enum(*external_owner, expression.value())) {
                    result = {TypeKind::enumeration,
                              external_owner->name + "." + enumeration->name};
                    model_.referenced_extension_abis_.insert(external_owner->provider_abi);
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_enum_type, "0", "", "",
                                                   result, 0, 0, false, true});
                    break;
                }
                for (const auto& enumeration : external_owner->enums) {
                    const auto found = std::find_if(
                        enumeration.entries.begin(), enumeration.entries.end(),
                        [&](const auto& entry) { return entry.name == expression.value(); });
                    if (found == enumeration.entries.end())
                        continue;
                    result = {TypeKind::enumeration, external_owner->name + "." + enumeration.name};
                    model_.referenced_extension_abis_.insert(external_owner->provider_abi);
                    model_.api_resolutions_.emplace(
                        &expression, ApiResolution{ApiResolutionKind::global_enum_value,
                                                   std::to_string(found->value), "", "", result, 0,
                                                   0, false, true});
                    break;
                }
                if (result.kind == TypeKind::enumeration)
                    break;
            }
            if (const auto* member =
                    script_symbols_->find_external_member(*external_owner, expression.value())) {
                if (member->kind == ScriptMemberKind::constant) {
                    if (!accessed_on_type) {
                        diagnostics_.error("GDS4058",
                                           "external constant '" + expression.value() +
                                               "' must be accessed on its type",
                                           expression.span);
                        result = unknown_type;
                    } else {
                        result = member->type;
                        model_.api_resolutions_.emplace(
                            &expression, ApiResolution{ApiResolutionKind::global_constant,
                                                       std::to_string(member->constant_value), "",
                                                       "", result, 0, 0, false, true});
                    }
                } else if (accessed_on_type) {
                    diagnostics_.error("GDS4058",
                                       "external instance member '" + expression.value() +
                                           "' cannot be accessed on its type",
                                       expression.span);
                    result = unknown_type;
                } else if (member->kind == ScriptMemberKind::function) {
                    result = {TypeKind::builtin, "Callable"};
                    model_.api_resolutions_.emplace(
                        &expression,
                        ApiResolution{ApiResolutionKind::external_callable, external_owner->name,
                                      "", "", result, 0, 0, false, false});
                } else if (member->kind == ScriptMemberKind::signal) {
                    result = {TypeKind::builtin, "Signal"};
                    model_.api_resolutions_.emplace(
                        &expression,
                        ApiResolution{ApiResolutionKind::external_signal, external_owner->name, "",
                                      "", result, 0, 0, false, false});
                } else {
                    result = member->type;
                    ApiResolution resolution{ApiResolutionKind::dynamic_property,
                                             external_owner->name,
                                             "",
                                             "",
                                             result,
                                             0,
                                             0,
                                             false,
                                             false};
                    resolution.read_only = member->read_only;
                    model_.api_resolutions_.emplace(&expression, std::move(resolution));
                }
                break;
            }
            if (external_owner->members_complete) {
                diagnostics_.error("GDS4112",
                                   "external type '" + external_owner->name +
                                       "' has no declared member '" + expression.value() + "'",
                                   expression.span);
                result = unknown_type;
                break;
            }
        }
        std::string owner;
        if (object_type.kind == TypeKind::array)
            owner = "Array";
        else if (object_type.kind == TypeKind::dictionary)
            owner = "Dictionary";
        else if (object_type.kind == TypeKind::string)
            owner = "String";
        else if (object_type.kind == TypeKind::string_name)
            owner = "StringName";
        else if (object_type.kind == TypeKind::builtin || object_type.kind == TypeKind::object)
            owner = object_type.name;
        const bool accessed_on_type =
            object_resolution && object_resolution->kind == ApiResolutionKind::type_reference;
        if (accessed_on_type) {
            if (api_.has_class_enum(owner, expression.value())) {
                result = {TypeKind::enumeration, owner + "." + expression.value()};
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::global_enum_type,
                                               "godot::" + owner + "::" + expression.value(), "",
                                               "", result, 0, 0, false, true});
                break;
            }
            if (const auto* constant = api_.find_builtin_constant(owner, expression.value())) {
                result = type_from_godot_api(constant->type);
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::builtin_constant, constant->value,
                                               "", "", result, 0, 0, false, true});
                break;
            }
        }
        if (const auto* constant = api_.find_class_constant(owner, expression.value())) {
            result = {TypeKind::integer, "int"};
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::global_constant,
                                                          std::to_string(constant->value), "", "",
                                                          result, 0, 0, false, true});
            break;
        }
        if (!accessed_on_type && api_.find_signal(owner, expression.value())) {
            result = {TypeKind::builtin, "Signal"};
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::script_signal, owner,
                                                          "", "", result, 0, 0, false, false});
        } else if (const auto* property = api_.find_property(owner, expression.value())) {
            result = type_from_godot_api(property->type);
            model_.api_resolutions_.emplace(
                &expression, ApiResolution{ApiResolutionKind::property, property->owner,
                                           property->getter, property->setter, result, 0, 0, false,
                                           property->direct, property->index});
        } else if (object_type.kind == TypeKind::dictionary) {
            // GDScript supports dictionary.key as syntax sugar for dictionary["key"].
            result = variant_type;
            model_.api_resolutions_.emplace(&expression,
                                            ApiResolution{ApiResolutionKind::dynamic_property, "",
                                                          "", "", result, 0, 0, false, false});
        } else {
            const bool dynamically_typed_object =
                object_type.kind == TypeKind::object &&
                (api_.find_class(object_type.name) ||
                 (script_symbols_ && script_symbols_->find_external(object_type.name)));
            if (dynamically_typed_object && !accessed_on_type) {
                result = variant_type;
                model_.api_resolutions_.emplace(
                    &expression, ApiResolution{ApiResolutionKind::dynamic_property, "", "", "",
                                               result, 0, 0, false, false});
            } else {
                result = unknown_type;
            }
        }
        break;
    }
    case ast::ExpressionKind::subscript: {
        const auto container = analyze_expression(*expression.operand(0));
        const auto index = analyze_expression(*expression.operand(1));
        if (container.kind == TypeKind::array || container.kind == TypeKind::string ||
            container.is_packed_array()) {
            require_assignable({TypeKind::integer, "int"}, index, expression.operand(1)->span,
                               "container index");
        }
        result = container_element_type(container, expression.span);
        break;
    }
    case ast::ExpressionKind::array_literal:
        for (std::size_t index = 0; index < expression.operand_count(); ++index)
            (void)analyze_expression(*expression.operand(index));
        result = {TypeKind::array, "Array"};
        break;
    case ast::ExpressionKind::dictionary_literal:
        for (std::size_t index = 0; index < expression.operand_count(); ++index)
            (void)analyze_expression(*expression.operand(index));
        result = {TypeKind::dictionary, "Dictionary"};
        break;
    }
    model_.expression_types_[&expression] = result;
    return result;
}

bool SemanticAnalyzer::is_match_value_pattern(const ast::Expression& expression) const {
    if (expression.kind() == ast::ExpressionKind::literal)
        return true;
    if (expression.kind() == ast::ExpressionKind::unary && expression.operand_count() == 1 &&
        (expression.value() == "+" || expression.value() == "-" || expression.value() == "~")) {
        return is_match_value_pattern(*expression.operand(0));
    }
    if (expression.kind() == ast::ExpressionKind::identifier) {
        const auto* symbol = model_.symbol_of(expression);
        if (symbol &&
            (symbol->kind == SymbolKind::constant || symbol->kind == SymbolKind::enum_value)) {
            return true;
        }
        const auto* resolution = model_.api_resolution_of(expression);
        return resolution && (resolution->kind == ApiResolutionKind::global_constant ||
                              resolution->kind == ApiResolutionKind::global_enum_value ||
                              resolution->kind == ApiResolutionKind::external_type_reference);
    }
    if (expression.kind() == ast::ExpressionKind::member) {
        const auto* resolution = model_.api_resolution_of(expression);
        return resolution && (resolution->kind == ApiResolutionKind::enum_member ||
                              resolution->kind == ApiResolutionKind::script_constant ||
                              resolution->kind == ApiResolutionKind::builtin_constant ||
                              resolution->kind == ApiResolutionKind::global_constant ||
                              resolution->kind == ApiResolutionKind::global_enum_value);
    }
    return false;
}

bool SemanticAnalyzer::is_assignment_target(const ast::Expression& expression) const noexcept {
    if (expression.kind() == ast::ExpressionKind::subscript)
        return expression.operand_count() == 2;
    if (expression.kind() == ast::ExpressionKind::identifier) {
        if (const auto* symbol = model_.symbol_of(expression)) {
            return symbol->kind == SymbolKind::field || symbol->kind == SymbolKind::parameter ||
                   symbol->kind == SymbolKind::local;
        }
    }
    if (expression.kind() != ast::ExpressionKind::identifier &&
        expression.kind() != ast::ExpressionKind::member) {
        return false;
    }
    const auto* resolution = model_.api_resolution_of(expression);
    if (!resolution)
        return false;
    return resolution->kind == ApiResolutionKind::property ||
           resolution->kind == ApiResolutionKind::script_property ||
           resolution->kind == ApiResolutionKind::dynamic_property ||
           resolution->kind == ApiResolutionKind::script_static_field;
}

SemanticAnalyzer::FlowResult SemanticAnalyzer::analyze_statement(const ast::Statement& statement) {
    switch (statement.kind()) {
    case ast::StatementKind::expression:
        (void)analyze_expression(*statement.expression());
        return FlowResult{true, false, false, false};
    case ast::StatementKind::return_statement: {
        const auto actual = statement.expression() ? analyze_expression(*statement.expression())
                                                   : (expected_return_.kind == TypeKind::void_type
                                                          ? Type{TypeKind::void_type, "void"}
                                                          : Type{TypeKind::nil, "null"});
        require_assignable(expected_return_, actual, statement.span, "invalid return value");
        return FlowResult{false, true, false, false};
    }
    case ast::StatementKind::await_statement: {
        const auto awaited = analyze_expression(*statement.expression());
        if (allowed_await_statements_.find(&statement) == allowed_await_statements_.end()) {
            diagnostics_.error("GDS4090",
                               "await is currently supported only at function-suite top level",
                               statement.span);
        }
        if (current_function_static_) {
            diagnostics_.error("GDS4091", "static functions cannot suspend on a signal yet",
                               statement.span);
        }
        if (expected_return_.kind != TypeKind::void_type && !allow_dynamic_await_return_) {
            diagnostics_.error("GDS4092",
                               "an AOT function containing await must currently return void",
                               statement.span);
        }
        if (!awaited.is_dynamic() &&
            (awaited.kind != TypeKind::builtin || awaited.name != "Signal")) {
            diagnostics_.error("GDS4093", "await requires a Signal expression", statement.span);
        }
        return FlowResult{true, false, false, false};
    }
    case ast::StatementKind::await_variable: {
        const auto awaited = analyze_expression(*statement.expression());
        if (allowed_await_statements_.find(&statement) == allowed_await_statements_.end()) {
            diagnostics_.error("GDS4090",
                               "await is currently supported only at function-suite top level",
                               statement.span);
        }
        if (current_function_static_) {
            diagnostics_.error("GDS4091", "static functions cannot suspend on a signal yet",
                               statement.span);
        }
        if (expected_return_.kind != TypeKind::void_type && !allow_dynamic_await_return_) {
            diagnostics_.error("GDS4092",
                               "an AOT function containing await must currently return void",
                               statement.span);
        }
        if (!awaited.is_dynamic() &&
            (awaited.kind != TypeKind::builtin || awaited.name != "Signal")) {
            diagnostics_.error("GDS4093", "await requires a Signal expression", statement.span);
        }
        const auto type =
            statement.type() ? type_from_name(*statement.type(), statement.span) : variant_type;
        model_.local_types_[&statement] = type;
        declare({SymbolKind::local, statement.name(), type, statement.span, false});
        return FlowResult{true, false, false, false};
    }
    case ast::StatementKind::assert_statement: {
        const auto condition = analyze_expression(*statement.condition());
        if (condition.kind == TypeKind::void_type) {
            diagnostics_.error("GDS4002", "assert condition cannot be void",
                               statement.condition()->span);
        }
        if (statement.expression()) {
            const auto message = analyze_expression(*statement.expression());
            require_assignable({TypeKind::string, "String"}, message, statement.expression()->span,
                               "assert message must be String");
        }
        return FlowResult{true, false, false, false};
    }
    case ast::StatementKind::variable: {
        const auto initializer =
            statement.expression() ? analyze_expression(*statement.expression()) : variant_type;
        Type type = statement.type().has_value() ? type_from_name(*statement.type(), statement.span)
                                                 : variant_type;
        if (statement.infer_type())
            type = initializer;
        if (statement.type().has_value() && statement.expression()) {
            require_assignable(type, initializer, statement.span, "invalid initializer");
        }
        model_.local_types_[&statement] = type;
        declare({SymbolKind::local, statement.name(), type, statement.span, false});
        return FlowResult{true, false, false, false};
    }
    case ast::StatementKind::assignment: {
        const auto target = analyze_expression(*statement.condition());
        const auto value = analyze_expression(*statement.expression());
        if (!is_assignment_target(*statement.condition())) {
            diagnostics_.error("GDS4110", "assignment target is not writable",
                               statement.condition()->span);
        }
        if (const auto* symbol = model_.symbol_of(*statement.condition());
            symbol && symbol->read_only) {
            diagnostics_.error("GDS4006", "cannot assign to constant '" + symbol->name + "'",
                               statement.span);
        }
        if (const auto* resolution = model_.api_resolution_of(*statement.condition());
            resolution && resolution->kind == ApiResolutionKind::property && !resolution->direct &&
            resolution->setter.empty()) {
            diagnostics_.error("GDS4012", "cannot assign to a read-only Godot property",
                               statement.span);
        }
        if (const auto* resolution = model_.api_resolution_of(*statement.condition());
            resolution && resolution->read_only) {
            diagnostics_.error("GDS4113", "cannot assign to a read-only extension property",
                               statement.span);
        }
        auto assigned = value;
        if (statement.operation() != "=") {
            const auto operation =
                statement.operation().substr(0, statement.operation().size() - 1);
            if (target.is_dynamic() || value.is_dynamic()) {
                assigned = variant_type;
            } else if (const auto* record =
                           api_.find_builtin_operator(builtin_operator_type(target), operation,
                                                      builtin_operator_type(value))) {
                assigned = type_from_godot_api(record->return_type);
            } else {
                diagnostics_.error("GDS4005",
                                   "operator '" + operation + "' is not defined for " +
                                       target.display_name() + " and " + value.display_name(),
                                   statement.span);
                assigned = unknown_type;
            }
        }
        require_assignable(target, assigned, statement.span, "invalid assignment");
        return FlowResult{true, false, false, false};
    }
    case ast::StatementKind::if_statement: {
        (void)analyze_expression(*statement.condition());
        scopes_.emplace_back();
        const auto body_flow = analyze_statements(statement.body());
        scopes_.pop_back();
        auto else_flow = FlowResult{true, false, false, false};
        if (!statement.else_body().empty()) {
            scopes_.emplace_back();
            else_flow = analyze_statements(statement.else_body());
            scopes_.pop_back();
        }
        return FlowResult{body_flow.falls_through || else_flow.falls_through,
                          body_flow.returns || else_flow.returns,
                          body_flow.breaks || else_flow.breaks,
                          body_flow.continues || else_flow.continues};
    }
    case ast::StatementKind::match_statement: {
        const auto matched_type = analyze_expression(*statement.condition());
        if (statement.match_branches().empty()) {
            diagnostics_.error("GDS4042", "match requires at least one branch", statement.span);
            return FlowResult{true, false, false, false};
        }
        bool unconditional_seen = false;
        FlowResult flow;
        for (const auto& branch : statement.match_branches()) {
            if (branch.patterns.empty()) {
                diagnostics_.error("GDS4043", "match branch requires at least one pattern",
                                   branch.span);
                flow.falls_through = true;
                continue;
            }
            if (unconditional_seen) {
                diagnostics_.error("GDS4044", "match branch is unreachable after a catch-all",
                                   branch.span);
            }
            scopes_.emplace_back();
            bool catch_all = false;
            for (const auto& pattern : branch.patterns) {
                if (pattern.kind() == ast::MatchPatternKind::value) {
                    const auto pattern_type = analyze_expression(*pattern.expression());
                    if (!is_match_value_pattern(*pattern.expression())) {
                        diagnostics_.error("GDS4045",
                                           "match value patterns must be compile-time constants",
                                           pattern.span);
                    }
                    const bool same_project_enum =
                        current_script_ && matched_type.kind == TypeKind::enumeration &&
                        pattern_type.kind == TypeKind::enumeration &&
                        (matched_type.name ==
                             current_script_->script_name + "." + pattern_type.name ||
                         pattern_type.name ==
                             current_script_->script_name + "." + matched_type.name);
                    if (!is_assignable(matched_type, pattern_type) &&
                        !is_assignable(pattern_type, matched_type) && !same_project_enum) {
                        diagnostics_.error("GDS4046",
                                           "match pattern type " + pattern_type.display_name() +
                                               " cannot match " + matched_type.display_name(),
                                           pattern.span);
                    }
                } else {
                    catch_all = true;
                    if (branch.patterns.size() != 1) {
                        diagnostics_.error("GDS4047",
                                           "wildcard and binding patterns cannot have alternatives",
                                           pattern.span);
                    }
                    if (pattern.kind() == ast::MatchPatternKind::binding) {
                        declare(
                            {SymbolKind::local, pattern.name(), matched_type, pattern.span, false});
                    }
                }
            }
            if (branch.guard) {
                const auto guard_type = analyze_expression(*branch.guard);
                require_assignable({TypeKind::boolean, "bool"}, guard_type, branch.guard->span,
                                   "invalid match guard");
            }
            const auto branch_flow = analyze_statements(branch.body);
            scopes_.pop_back();
            flow.falls_through = flow.falls_through || branch_flow.falls_through;
            flow.returns = flow.returns || branch_flow.returns;
            flow.breaks = flow.breaks || branch_flow.breaks;
            flow.continues = flow.continues || branch_flow.continues;
            if (catch_all && !branch.guard)
                unconditional_seen = true;
        }
        if (!unconditional_seen)
            flow.falls_through = true;
        return flow;
    }
    case ast::StatementKind::while_statement: {
        (void)analyze_expression(*statement.condition());
        ++loop_depth_;
        scopes_.emplace_back();
        const auto body_flow = analyze_statements(statement.body());
        scopes_.pop_back();
        --loop_depth_;
        const bool constant_true =
            statement.condition()->kind() == ast::ExpressionKind::literal &&
            statement.condition()->literal_kind() == ast::LiteralKind::boolean &&
            statement.condition()->value() == "true";
        return FlowResult{!constant_true || body_flow.breaks, body_flow.returns, false, false};
    }
    case ast::StatementKind::for_statement: {
        const auto iterable = analyze_expression(*statement.condition());
        if (!iterable.is_dynamic() && iterable.kind != TypeKind::array &&
            iterable.kind != TypeKind::dictionary && iterable.kind != TypeKind::string &&
            iterable.kind != TypeKind::integer && !iterable.is_packed_array()) {
            diagnostics_.error("GDS4007", "for loop expression is not iterable", statement.span);
        }
        ++loop_depth_;
        scopes_.emplace_back();
        const auto inferred_element_type = container_element_type(iterable, statement.span);
        const auto element_type = statement.type()
                                      ? type_from_name(*statement.type(), statement.span)
                                      : inferred_element_type;
        if (statement.type())
            require_assignable(element_type, inferred_element_type, statement.span,
                               "invalid iterator variable type");
        model_.local_types_[&statement] = element_type;
        declare({SymbolKind::local, statement.name(), element_type, statement.span, false});
        const auto body_flow = analyze_statements(statement.body());
        scopes_.pop_back();
        --loop_depth_;
        return FlowResult{true, body_flow.returns, false, false};
    }
    case ast::StatementKind::break_statement:
    case ast::StatementKind::continue_statement: {
        if (loop_depth_ == 0) {
            diagnostics_.error("GDS4008", "loop control statement used outside a loop",
                               statement.span);
        }
        return statement.kind() == ast::StatementKind::break_statement
                   ? FlowResult{false, false, true, false}
                   : FlowResult{false, false, false, true};
    }
    case ast::StatementKind::pass_statement:
        return FlowResult{true, false, false, false};
    }
    return FlowResult{true, false, false, false};
}

SemanticAnalyzer::FlowResult
SemanticAnalyzer::analyze_statements(const std::vector<ast::Statement>& statements) {
    FlowResult flow{true, false, false, false};
    for (const auto& statement : statements) {
        const bool reachable = flow.falls_through;
        const bool ignores_unreachable = std::any_of(
            statement.annotations.begin(), statement.annotations.end(),
            [](const ast::Annotation& annotation) {
                return annotation.name == "warning_ignore" && annotation.arguments.size() == 1 &&
                       annotation.arguments.front()->kind() == ast::ExpressionKind::literal &&
                       annotation.arguments.front()->value() == "unreachable_code";
            });
        if (!reachable && !ignores_unreachable) {
            diagnostics_.error("GDS4069", "unreachable statement", statement.span);
        }
        const auto statement_flow = analyze_statement(statement);
        if (!reachable)
            continue;
        flow.falls_through = statement_flow.falls_through;
        flow.returns = flow.returns || statement_flow.returns;
        flow.breaks = flow.breaks || statement_flow.breaks;
        flow.continues = flow.continues || statement_flow.continues;
    }
    return flow;
}

void SemanticAnalyzer::analyze_function(const ast::FunctionDeclaration& function) {
    const auto previous_in_function = in_function_;
    const auto previous_function_name = current_function_name_;
    in_function_ = true;
    current_function_name_ = function.name;
    for (const auto& annotation : function.annotations) {
        if (annotation.name == "warning_ignore")
            continue;
        diagnostics_.error("GDS4112",
                           "function annotation '@" + annotation.name +
                               "' is recognized but its runtime lowering is not implemented",
                           annotation.span);
    }
    if (function.name == "_init" && function.is_static)
        diagnostics_.error("GDS4065", "_init cannot be static", function.span);
    if (function.name == "_init" && function.return_type && *function.return_type != "void") {
        diagnostics_.error("GDS4066", "_init cannot declare a non-void return type", function.span);
    }
    const auto* virtual_method =
        function.is_static ? nullptr : api_.find_method(base_type_, function.name);
    if (virtual_method && !virtual_method->is_virtual)
        virtual_method = nullptr;
    const ScriptMemberSymbol* inherited_script_method = nullptr;
    if (!function.is_static && function.name != "_init" && script_symbols_ && current_script_) {
        if (const auto* base = script_symbols_->base_of(*current_script_)) {
            const auto* inherited = script_symbols_->find_member(*base, function.name);
            if (inherited && inherited->kind == ScriptMemberKind::function &&
                !inherited->is_static) {
                inherited_script_method = inherited;
            }
        }
    }
    expected_return_ =
        function.name == "_init"           ? void_type
        : function.return_type.has_value() ? type_from_name(*function.return_type, function.span)
        : inherited_script_method          ? inherited_script_method->type
        : virtual_method && std::string_view{virtual_method->return_type}.empty() ? void_type
        : virtual_method ? type_from_godot_api(virtual_method->return_type)
                         : variant_type;
    if (virtual_method && function.return_type) {
        const auto api_return = std::string_view{virtual_method->return_type}.empty()
                                    ? void_type
                                    : type_from_godot_api(virtual_method->return_type);
        if (expected_return_ != api_return) {
            diagnostics_.error("GDS4120",
                               "Godot virtual override '" + function.name + "' must return " +
                                   api_return.display_name() + ", got " +
                                   expected_return_.display_name(),
                               function.span);
        }
    }
    if (virtual_method &&
        std::any_of(function.parameters.begin(), function.parameters.end(),
                    [](const auto& parameter) { return parameter.default_value != nullptr; })) {
        diagnostics_.error("GDS4118",
                           "Godot virtual override '" + function.name +
                               "' cannot redeclare default arguments in the native ABI",
                           function.span);
    }
    model_.function_return_types_[&function] = expected_return_;
    allowed_await_statements_.clear();
    collect_structured_awaits(function.body, allowed_await_statements_);
    if (function.name == "_init") {
        for (const auto* statement : allowed_await_statements_) {
            diagnostics_.error("GDS4097", "_init cannot suspend on a signal", statement->span);
        }
    }
    const bool value_returning_dynamic_coroutine = !function.return_type &&
                                                   contains_value_return(function.body) &&
                                                   !allowed_await_statements_.empty();
    allow_dynamic_await_return_ = !function.return_type;
    if (value_returning_dynamic_coroutine) {
        diagnostics_.warning(
            "GDS4103",
            "a value-returning dynamic coroutine is emitted as fire-and-forget; values returned "
            "after suspension are not observable until native awaitable results are enabled",
            function.span);
    }
    current_function_static_ = function.is_static;
    current_accessor_fields_.clear();
    if (const auto found = bound_accessor_fields_.find(function.name);
        found != bound_accessor_fields_.end()) {
        current_accessor_fields_ = found->second;
    }
    scopes_.emplace_back();
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        const auto* virtual_argument = virtual_method && index < virtual_method->maximum_arguments
                                           ? api_.argument(*virtual_method, index)
                                           : nullptr;
        const Type* inherited_argument =
            inherited_script_method && index < inherited_script_method->parameters.size()
                ? &inherited_script_method->parameters[index]
                : nullptr;
        std::optional<Type> analyzed_default;
        if (parameter.default_value)
            analyzed_default = analyze_expression(*parameter.default_value);
        const auto type = parameter.type.has_value()
                              ? type_from_name(*parameter.type, parameter.span)
                          : parameter.infer_type && analyzed_default ? *analyzed_default
                          : inherited_argument                       ? *inherited_argument
                          : virtual_argument ? type_from_godot_api(virtual_argument->type)
                                             : variant_type;
        if (virtual_argument && parameter.type) {
            const auto api_type = type_from_godot_api(virtual_argument->type);
            if (type != api_type) {
                diagnostics_.error("GDS4121",
                                   "parameter " + std::to_string(index + 1) +
                                       " of Godot virtual override '" + function.name +
                                       "' must be " + api_type.display_name() + ", got " +
                                       type.display_name(),
                                   parameter.span);
            }
        }
        model_.parameter_types_[&parameter] = type;
        declare({SymbolKind::parameter, parameter.name, type, parameter.span, false});
        if (analyzed_default)
            require_assignable(type, *analyzed_default, parameter.span, "invalid default value");
    }
    if (virtual_method && function.parameters.size() != virtual_method->maximum_arguments) {
        diagnostics_.error("GDS4102",
                           "Godot virtual method '" + function.name + "' expects " +
                               std::to_string(virtual_method->maximum_arguments) + " parameter(s)",
                           function.span);
    }
    const auto flow = analyze_statements(function.body);
    scopes_.pop_back();
    if (expected_return_.kind != TypeKind::void_type && !expected_return_.is_dynamic() &&
        flow.falls_through) {
        diagnostics_.error("GDS4009",
                           "function returning " + expected_return_.display_name() +
                               " does not return a value on every reachable path",
                           function.span);
    }
    current_accessor_fields_.clear();
    allowed_await_statements_.clear();
    allow_dynamic_await_return_ = false;
    current_function_static_ = false;
    in_function_ = previous_in_function;
    current_function_name_ = previous_function_name;
}

void SemanticAnalyzer::analyze_lambda(const ast::LambdaExpression& expression) {
    const auto previous_return = expected_return_;
    const auto previous_static = current_function_static_;
    const auto previous_in_function = in_function_;
    const auto previous_loop_depth = loop_depth_;
    auto previous_await = std::move(allowed_await_statements_);
    const auto previous_dynamic_await = allow_dynamic_await_return_;

    expected_return_ = expression.return_type
                           ? type_from_name(*expression.return_type, expression.span)
                           : variant_type;
    model_.lambda_return_types_[&expression] = expected_return_;
    if (in_function_ && !current_function_static_)
        model_.owner_bound_lambdas_.insert(&expression);
    current_function_static_ = false;
    in_function_ = true;
    loop_depth_ = 0;
    allowed_await_statements_.clear();
    collect_structured_awaits(expression.body, allowed_await_statements_);
    allow_dynamic_await_return_ = !expression.return_type;
    scopes_.emplace_back();
    for (const auto& parameter : expression.parameters) {
        std::optional<Type> analyzed_default;
        if (parameter.default_value)
            analyzed_default = analyze_expression(*parameter.default_value);
        const auto type = parameter.type ? type_from_name(*parameter.type, parameter.span)
                          : parameter.infer_type && analyzed_default ? *analyzed_default
                                                                     : variant_type;
        model_.parameter_types_[&parameter] = type;
        declare({SymbolKind::parameter, parameter.name, type, parameter.span, false});
        if (analyzed_default)
            require_assignable(type, *analyzed_default, parameter.span,
                               "invalid lambda default value");
    }
    const auto flow = analyze_statements(expression.body);
    scopes_.pop_back();
    if (expected_return_.kind != TypeKind::void_type && !expected_return_.is_dynamic() &&
        flow.falls_through) {
        diagnostics_.error("GDS4009",
                           "lambda returning " + expected_return_.display_name() +
                               " does not return a value on every reachable path",
                           expression.span);
    }

    allowed_await_statements_ = std::move(previous_await);
    allow_dynamic_await_return_ = previous_dynamic_await;
    loop_depth_ = previous_loop_depth;
    in_function_ = previous_in_function;
    current_function_static_ = previous_static;
    expected_return_ = previous_return;
}

void SemanticAnalyzer::analyze_class(const ast::ClassDeclaration& declaration) {
    for (const auto& annotation : declaration.annotations) {
        if (annotation.name == "warning_ignore")
            continue;
        diagnostics_.error("GDS4113",
                           "internal class annotation '@" + annotation.name +
                               "' is recognized but its lowering is not implemented",
                           annotation.span);
    }
    auto saved_scopes = std::move(scopes_);
    auto saved_enum_types = std::move(enum_types_);
    auto saved_enum_members = std::move(enum_members_);
    auto saved_accessor_fields = std::move(accessor_fields_);
    auto saved_static_fields = std::move(static_fields_);
    auto saved_current_accessor_fields = std::move(current_accessor_fields_);
    auto saved_bound_accessor_fields = std::move(bound_accessor_fields_);
    auto saved_functions = std::move(functions_);
    auto saved_allowed_await = std::move(allowed_await_statements_);
    const auto saved_dynamic_await = allow_dynamic_await_return_;
    const auto saved_static = current_function_static_;
    const auto saved_in_function = in_function_;
    const auto saved_return = expected_return_;
    const auto saved_base = base_type_;
    const auto saved_loop_depth = loop_depth_;
    const auto* saved_inner = current_inner_class_;

    scopes_.clear();
    scopes_.emplace_back();
    scopes_.emplace_back();
    enum_types_.clear();
    enum_members_.clear();
    accessor_fields_.clear();
    static_fields_.clear();
    current_accessor_fields_.clear();
    bound_accessor_fields_.clear();
    functions_.clear();
    allowed_await_statements_.clear();
    allow_dynamic_await_return_ = false;
    current_function_static_ = false;
    in_function_ = false;
    expected_return_ = void_type;
    loop_depth_ = 0;
    base_type_ = declaration.base_type.value_or("RefCounted");
    current_inner_class_ = find_inner_class(declaration.name);

    if (!api_.find_class(base_type_)) {
        diagnostics_.error("GDS4099",
                           "internal class base '" + base_type_ + "' is not a Godot engine type",
                           declaration.span);
        base_type_ = "RefCounted";
    }
    if (!declaration.classes.empty()) {
        diagnostics_.error("GDS4100", "nested internal classes are not supported yet",
                           declaration.classes.front().span);
    }
    analyze_enums(declaration.enums);
    for (const auto& function : declaration.functions)
        functions_.emplace(function.name, &function);
    for (const auto& variable : declaration.variables) {
        if (variable.is_static)
            static_fields_.insert(variable.name);
        if (variable.getter || variable.setter) {
            accessor_fields_.insert(variable.name);
            if (variable.getter && !variable.getter->method.empty())
                bound_accessor_fields_[variable.getter->method].insert(variable.name);
            if (variable.setter && !variable.setter->method.empty())
                bound_accessor_fields_[variable.setter->method].insert(variable.name);
        }
        Type type = variable.type ? type_from_name(*variable.type, variable.span) : variant_type;
        if (variable.infer_type || (variable.is_constant && !variable.type))
            type = unknown_type;
        declare({variable.is_constant ? SymbolKind::constant : SymbolKind::field, variable.name,
                 type, variable.span, variable.is_constant});
    }
    for (const auto& signal : declaration.signals) {
        declare(
            {SymbolKind::signal, signal.name, {TypeKind::builtin, "Signal"}, signal.span, true});
    }
    for (const auto& function : declaration.functions) {
        const auto type = function.name == "_init" ? void_type
                          : function.return_type
                              ? type_from_name(*function.return_type, function.span)
                              : variant_type;
        declare({SymbolKind::function, function.name, type, function.span, true});
    }
    const auto analyze_internal_variable = [&](const ast::VariableDeclaration& variable) {
        const auto initializer =
            variable.initializer ? analyze_expression(*variable.initializer) : variant_type;
        Type type = variable.type ? type_from_name(*variable.type, variable.span) : variant_type;
        if (variable.infer_type || (variable.is_constant && !variable.type)) {
            type = initializer;
        } else if (variable.type && variable.initializer) {
            require_assignable(type, initializer, variable.span,
                               "invalid internal field initializer");
        }
        model_.variable_types_[&variable] = type;
        const auto implied_property = !variable.type && !variable.infer_type
                                          ? implied_export_property_type(variable)
                                          : std::nullopt;
        const auto property_type = implied_property ? *implied_property
                                   : has_property_annotation(variable) && !variable.type &&
                                           !variable.infer_type && variable.initializer
                                       ? initializer
                                       : type;
        model_.property_types_[&variable] = property_type;
        validate_annotations(variable, property_type);
        if (const auto found = scopes_.back().find(variable.name); found != scopes_.back().end())
            found->second.type = type;
    };
    // Constants are visible throughout a GDScript class regardless of textual order. Resolve
    // them before ordinary field initializers so forward constant references keep their native
    // scalar type instead of degrading to Variant.
    for (const auto& variable : declaration.variables) {
        if (variable.is_constant)
            analyze_internal_variable(variable);
    }
    for (const auto& variable : declaration.variables) {
        if (!variable.is_constant)
            analyze_internal_variable(variable);
    }
    for (const auto& signal : declaration.signals) {
        std::unordered_set<std::string> names;
        for (const auto& parameter : signal.parameters) {
            const auto type =
                parameter.type ? type_from_name(*parameter.type, parameter.span) : variant_type;
            model_.parameter_types_[&parameter] = type;
            if (!names.insert(parameter.name).second) {
                diagnostics_.error("GDS4010", "duplicate signal parameter '" + parameter.name + "'",
                                   parameter.span);
            }
        }
    }
    for (const auto& variable : declaration.variables)
        analyze_property_accessors(variable, model_.type_of(variable));
    for (const auto& function : declaration.functions)
        analyze_function(function);

    scopes_ = std::move(saved_scopes);
    enum_types_ = std::move(saved_enum_types);
    enum_members_ = std::move(saved_enum_members);
    accessor_fields_ = std::move(saved_accessor_fields);
    static_fields_ = std::move(saved_static_fields);
    current_accessor_fields_ = std::move(saved_current_accessor_fields);
    bound_accessor_fields_ = std::move(saved_bound_accessor_fields);
    functions_ = std::move(saved_functions);
    allowed_await_statements_ = std::move(saved_allowed_await);
    allow_dynamic_await_return_ = saved_dynamic_await;
    current_function_static_ = saved_static;
    in_function_ = saved_in_function;
    expected_return_ = saved_return;
    base_type_ = saved_base;
    loop_depth_ = saved_loop_depth;
    current_inner_class_ = saved_inner;
}

void SemanticAnalyzer::analyze_property_accessors(const ast::VariableDeclaration& variable,
                                                  const Type& type) {
    if (!variable.getter && !variable.setter)
        return;
    if (variable.is_constant) {
        diagnostics_.error("GDS4049", "constants cannot have property accessors", variable.span);
        return;
    }
    current_accessor_fields_.clear();
    current_accessor_fields_.insert(variable.name);
    auto saved_allowed_await = std::move(allowed_await_statements_);
    const auto saved_return = expected_return_;
    const auto saved_dynamic_await = allow_dynamic_await_return_;
    const auto saved_static = current_function_static_;
    const auto saved_in_function = in_function_;
    const auto prepare_accessor_body = [&](const std::vector<ast::Statement>& body,
                                           const Type& return_type) {
        allowed_await_statements_.clear();
        collect_structured_awaits(body, allowed_await_statements_);
        expected_return_ = return_type;
        allow_dynamic_await_return_ = false;
        current_function_static_ = variable.is_static;
        in_function_ = true;
    };
    if (variable.getter) {
        if (!variable.getter->method.empty()) {
            const auto found = functions_.find(variable.getter->method);
            if (found == functions_.end()) {
                diagnostics_.error("GDS4082",
                                   "property getter method '" + variable.getter->method +
                                       "' was not found",
                                   variable.getter->span);
            } else {
                const auto& function = *found->second;
                if (!function.parameters.empty()) {
                    diagnostics_.error("GDS4083", "property getter method must accept no arguments",
                                       variable.getter->span);
                }
                if (function.is_static != variable.is_static) {
                    diagnostics_.error("GDS4084",
                                       "property getter method staticness must match its field",
                                       variable.getter->span);
                }
                const auto return_type = function.return_type
                                             ? type_from_name(*function.return_type, function.span)
                                             : variant_type;
                require_assignable(type, return_type, variable.getter->span,
                                   "property getter result");
            }
        } else {
            prepare_accessor_body(variable.getter->body, type);
            scopes_.emplace_back();
            const auto flow = analyze_statements(variable.getter->body);
            scopes_.pop_back();
            if (flow.falls_through) {
                diagnostics_.error("GDS4050", "property getter must return a value on every path",
                                   variable.getter->span);
            }
        }
    }
    if (variable.setter) {
        if (!variable.setter->method.empty()) {
            const auto found = functions_.find(variable.setter->method);
            if (found == functions_.end()) {
                diagnostics_.error("GDS4085",
                                   "property setter method '" + variable.setter->method +
                                       "' was not found",
                                   variable.setter->span);
            } else {
                const auto& function = *found->second;
                if (function.parameters.size() != 1) {
                    diagnostics_.error("GDS4086",
                                       "property setter method must accept exactly one argument",
                                       variable.setter->span);
                } else {
                    const auto parameter_type =
                        function.parameters.front().type
                            ? type_from_name(*function.parameters.front().type,
                                             function.parameters.front().span)
                            : variant_type;
                    require_assignable(parameter_type, type, variable.setter->span,
                                       "property setter argument");
                }
                if (function.is_static != variable.is_static) {
                    diagnostics_.error("GDS4087",
                                       "property setter method staticness must match its field",
                                       variable.setter->span);
                }
                if (function.return_type && *function.return_type != "void") {
                    diagnostics_.error("GDS4088", "property setter method must return void",
                                       variable.setter->span);
                }
            }
        } else {
            prepare_accessor_body(variable.setter->body, void_type);
            if (variable.setter->parameter.empty()) {
                diagnostics_.error("GDS4051", "property setter requires a parameter",
                                   variable.setter->span);
            }
            expected_return_ = void_type;
            scopes_.emplace_back();
            declare({SymbolKind::parameter, variable.setter->parameter, type, variable.setter->span,
                     false});
            (void)analyze_statements(variable.setter->body);
            scopes_.pop_back();
        }
    }
    allowed_await_statements_ = std::move(saved_allowed_await);
    expected_return_ = saved_return;
    allow_dynamic_await_return_ = saved_dynamic_await;
    current_function_static_ = saved_static;
    in_function_ = saved_in_function;
    current_accessor_fields_.clear();
}

void SemanticAnalyzer::analyze_enums(const std::vector<ast::EnumDeclaration>& declarations) {
    for (const auto& declaration : declarations) {
        if (declaration.name) {
            if (!enum_types_.insert(*declaration.name).second) {
                diagnostics_.error("GDS4036", "duplicate enum type '" + *declaration.name + "'",
                                   declaration.span);
            } else {
                const auto qualified_name =
                    current_inner_class_ ? current_inner_class_->name + "." + *declaration.name
                    : current_script_    ? current_script_->script_name + "." + *declaration.name
                                         : *declaration.name;
                declare({SymbolKind::enum_type,
                         *declaration.name,
                         {TypeKind::enumeration, qualified_name},
                         declaration.span,
                         true});
            }
        }
    }

    for (const auto& declaration : declarations) {
        if (declaration.entries.empty()) {
            diagnostics_.error("GDS4037", "an enum must declare at least one member",
                               declaration.span);
            continue;
        }
        std::unordered_map<std::string, std::int64_t> values;
        std::int64_t next_value = 0;
        for (const auto& entry : declaration.entries) {
            if (values.find(entry.name) != values.end()) {
                diagnostics_.error("GDS4038", "duplicate enum member '" + entry.name + "'",
                                   entry.span);
                continue;
            }
            auto value = entry.value ? evaluate_integer_constant(*entry.value, values)
                                     : std::optional<std::int64_t>{next_value};
            if (!value) {
                diagnostics_.error("GDS4039",
                                   "enum member '" + entry.name +
                                       "' requires a valid integer constant expression",
                                   entry.span);
                value = 0;
            }
            values.emplace(entry.name, *value);
            model_.enum_values_[&entry] = *value;
            if (!declaration.name) {
                declare({SymbolKind::enum_value,
                         entry.name,
                         {TypeKind::integer, "int"},
                         entry.span,
                         true});
            }
            if (*value == std::numeric_limits<std::int64_t>::max()) {
                if (&entry != &declaration.entries.back()) {
                    diagnostics_.error("GDS4040", "automatic enum value overflows int64",
                                       entry.span);
                }
                next_value = 0;
            } else {
                next_value = *value + 1;
            }
        }
        if (declaration.name) {
            const auto qualified_name =
                current_inner_class_ ? current_inner_class_->name + "." + *declaration.name
                : current_script_    ? current_script_->script_name + "." + *declaration.name
                                     : *declaration.name;
            enum_members_[qualified_name] = std::move(values);
        }
    }
}

void SemanticAnalyzer::validate_annotations(const ast::VariableDeclaration& variable,
                                            const Type& type) {
    if (variable.annotations.empty())
        return;

    const ast::Annotation* property_annotation = nullptr;
    for (const auto& candidate : variable.annotations) {
        if (candidate.name == "onready") {
            if (!candidate.arguments.empty()) {
                diagnostics_.error("GDS4021", "@onready does not accept arguments", candidate.span);
            }
            if (variable.is_constant) {
                diagnostics_.error("GDS4024", "@onready cannot be applied to constants",
                                   candidate.span);
            }
            if (variable.is_static) {
                diagnostics_.error("GDS4024", "@onready cannot be applied to static variables",
                                   candidate.span);
            }
            continue;
        }
        if (candidate.name == "warning_ignore")
            continue;
        if (candidate.name == "export_group" || candidate.name == "export_subgroup" ||
            candidate.name == "export_category") {
            const auto maximum =
                candidate.name == "export_category" ? std::size_t{1} : std::size_t{2};
            if (candidate.arguments.empty() || candidate.arguments.size() > maximum) {
                diagnostics_.error("GDS4022",
                                   "@" + candidate.name +
                                       (maximum == 1 ? " expects one string argument"
                                                     : " expects one or two string arguments"),
                                   candidate.span);
            }
            for (const auto& argument : candidate.arguments) {
                if (!is_string_literal(*argument)) {
                    diagnostics_.error("GDS4023",
                                       "@" + candidate.name + " arguments must be string literals",
                                       argument->span);
                }
            }
            continue;
        }
        if (property_annotation) {
            diagnostics_.error("GDS4020", "a field accepts at most one export annotation",
                               candidate.span);
        } else {
            property_annotation = &candidate;
        }
    }
    if (!property_annotation)
        return;
    const auto& annotation = *property_annotation;
    const auto& name = annotation.name;
    const auto argument_count = annotation.arguments.size();
    const auto require_no_arguments = [&] {
        if (argument_count != 0) {
            diagnostics_.error("GDS4021", "@" + name + " does not accept arguments",
                               annotation.span);
            return false;
        }
        return true;
    };
    const auto require_string_arguments = [&](std::size_t minimum, std::size_t maximum) {
        if (argument_count < minimum || argument_count > maximum) {
            diagnostics_.error("GDS4022",
                               "@" + name + " expects " + std::to_string(minimum) +
                                   (minimum == maximum ? "" : " to " + std::to_string(maximum)) +
                                   " string argument(s)",
                               annotation.span);
            return false;
        }
        bool valid = true;
        for (const auto& argument : annotation.arguments) {
            if (!is_string_literal(*argument)) {
                diagnostics_.error("GDS4023", "@" + name + " arguments must be string literals",
                                   argument->span);
                valid = false;
            }
        }
        return valid;
    };
    if (variable.is_constant) {
        diagnostics_.error("GDS4024", "export annotations cannot be applied to constants",
                           annotation.span);
        return;
    }
    if (variable.is_static) {
        diagnostics_.error("GDS4024", "export annotations cannot be applied to static variables",
                           annotation.span);
        return;
    }
    const bool unchecked_export = name == "export_storage" || name == "export_custom";
    if (!unchecked_export &&
        (type.is_dynamic() || type.kind == TypeKind::nil || type.kind == TypeKind::void_type)) {
        diagnostics_.error("GDS4025", "exported fields require a concrete serializable type",
                           variable.span);
    }
    const auto* script_type = script_symbols_ && type.kind == TypeKind::object
                                  ? script_symbols_->find_global(type.name)
                                  : nullptr;
    const bool script_serializable =
        script_type && (api_.inherits(script_type->godot_base_type, "Node") ||
                        api_.inherits(script_type->godot_base_type, "Resource"));
    if (!unchecked_export && type.kind == TypeKind::object && !api_.inherits(type.name, "Node") &&
        !api_.inherits(type.name, "Resource") && !script_serializable) {
        diagnostics_.error("GDS4035", "exported object fields must derive from Node or Resource",
                           variable.span);
    }

    const bool string_collection =
        type.kind == TypeKind::string ||
        (type.kind == TypeKind::array && type.name == "Array[String]") ||
        (type.kind == TypeKind::builtin && type.name == "PackedStringArray");
    if (name == "export") {
        require_no_arguments();
    } else if (name == "export_range") {
        if (type.kind != TypeKind::integer && type.kind != TypeKind::floating) {
            diagnostics_.error("GDS4026", "@export_range requires an int or float field",
                               variable.span);
        }
        if (argument_count < 2) {
            diagnostics_.error("GDS4027", "@export_range requires minimum and maximum values",
                               annotation.span);
        }
        for (std::size_t index = 0; index < argument_count; ++index) {
            const bool valid = index < 3 ? is_number_literal(*annotation.arguments[index])
                                         : is_string_literal(*annotation.arguments[index]);
            if (!valid) {
                diagnostics_.error("GDS4028",
                                   "@export_range expects numeric bounds/step followed by "
                                   "optional string flags",
                                   annotation.arguments[index]->span);
            }
        }
    } else if (name == "export_enum") {
        if (type.kind != TypeKind::integer && type.kind != TypeKind::string) {
            diagnostics_.error("GDS4029", "@export_enum requires an int or String field",
                               variable.span);
        }
        require_string_arguments(1, 256);
    } else if (name == "export_flags") {
        if (type.kind != TypeKind::integer) {
            diagnostics_.error("GDS4030", "@export_flags requires an int field", variable.span);
        }
        require_string_arguments(1, 32);
    } else if (name == "export_file" || name == "export_file_path" ||
               name == "export_global_file") {
        if (!string_collection) {
            diagnostics_.error("GDS4031", "@" + name + " requires a String collection field",
                               variable.span);
        }
        require_string_arguments(0, 256);
    } else if (name == "export_dir" || name == "export_global_dir") {
        if (!string_collection) {
            diagnostics_.error("GDS4031", "@" + name + " requires a String collection field",
                               variable.span);
        }
        require_no_arguments();
    } else if (name == "export_multiline") {
        const bool multiline_type =
            string_collection || type.kind == TypeKind::dictionary ||
            (type.kind == TypeKind::array && type.name == "Array[Dictionary]");
        if (!multiline_type) {
            diagnostics_.error("GDS4031", "@export_multiline requires text or Dictionary values",
                               variable.span);
        }
        require_string_arguments(0, 256);
    } else if (name == "export_color_no_alpha") {
        if (type.kind != TypeKind::builtin || type.name != "Color") {
            diagnostics_.error("GDS4032", "@export_color_no_alpha requires a Color field",
                               variable.span);
        }
        require_no_arguments();
    } else if (name == "export_node_path") {
        if (type.kind != TypeKind::builtin || type.name != "NodePath") {
            diagnostics_.error("GDS4033", "@export_node_path requires a NodePath field",
                               variable.span);
        }
        require_string_arguments(0, 64);
    } else if (name == "export_placeholder") {
        if (!string_collection) {
            diagnostics_.error("GDS4031", "@export_placeholder requires a String collection field",
                               variable.span);
        }
        require_string_arguments(1, 1);
    } else if (name == "export_storage") {
        require_no_arguments();
    } else if (name == "export_tool_button") {
        if (!script_tool_) {
            diagnostics_.error("GDS4036", "@export_tool_button requires a @tool script",
                               annotation.span);
        }
        if (type.kind != TypeKind::builtin || type.name != "Callable") {
            diagnostics_.error("GDS4037", "@export_tool_button requires a Callable field",
                               variable.span);
        }
        require_string_arguments(1, 2);
    } else if (name == "export_custom") {
        if (argument_count < 2 || argument_count > 3) {
            diagnostics_.error("GDS4022", "@export_custom expects 2 or 3 arguments",
                               annotation.span);
        }
        if (argument_count >= 2 && !is_string_literal(*annotation.arguments[1])) {
            diagnostics_.error("GDS4023", "@export_custom hint string must be a string literal",
                               annotation.arguments[1]->span);
        }
    } else {
        diagnostics_.error("GDS4034", "unsupported annotation '@" + name + "'", annotation.span);
    }
}

SemanticModel SemanticAnalyzer::analyze(const ast::Script& script) {
    model_ = {};
    scopes_.clear();
    enum_types_.clear();
    enum_members_.clear();
    accessor_fields_.clear();
    static_fields_.clear();
    current_accessor_fields_.clear();
    bound_accessor_fields_.clear();
    functions_.clear();
    local_inner_classes_.clear();
    allowed_await_statements_.clear();
    allow_dynamic_await_return_ = false;
    current_function_static_ = false;
    script_tool_ = script.tool;
    for (const auto& annotation : script.annotations) {
        if (annotation.name == "icon" && (annotation.arguments.size() != 1 ||
                                          !is_string_literal(*annotation.arguments.front()))) {
            diagnostics_.error("GDS4114", "@icon expects one string literal path", annotation.span);
        }
    }
    current_script_ = script_symbols_ ? script_symbols_->find_path(current_script_path_) : nullptr;
    if (script_symbols_ && current_script_)
        record_script_dependency(script_symbols_->base_of(*current_script_));
    Scope inherited_scope;
    if (script_symbols_ && current_script_) {
        for (const auto* member : script_symbols_->inherited_members(*current_script_)) {
            auto kind = SymbolKind::field;
            if (member->kind == ScriptMemberKind::constant)
                kind = SymbolKind::constant;
            else if (member->kind == ScriptMemberKind::enum_value)
                kind = SymbolKind::enum_value;
            else if (member->kind == ScriptMemberKind::function)
                kind = SymbolKind::function;
            else if (member->kind == ScriptMemberKind::signal)
                kind = SymbolKind::signal;
            inherited_scope.emplace(member->name,
                                    Symbol{kind,
                                           member->name,
                                           member->type,
                                           {},
                                           member->kind == ScriptMemberKind::constant ||
                                               member->kind == ScriptMemberKind::enum_value});
            if (member->kind == ScriptMemberKind::field && member->has_accessor)
                accessor_fields_.insert(member->name);
            if (member->kind == ScriptMemberKind::field && member->is_static)
                static_fields_.insert(member->name);
        }
    }
    scopes_.push_back(std::move(inherited_scope));
    scopes_.emplace_back();
    const auto declared_base = script.base_type.value_or("Node");
    base_type_ = semantic_base_type_.empty() ? declared_base : semantic_base_type_;
    if (semantic_base_type_.empty() && !api_.find_class(base_type_)) {
        diagnostics_.error("GDS4052",
                           "base script '" + declared_base +
                               "' requires project-level inheritance resolution",
                           script.span);
    }
    for (const auto& declaration : script.classes) {
        ScriptInnerClassSymbol symbol;
        symbol.name = declaration.name;
        symbol.godot_base_type = declaration.base_type.value_or("RefCounted");
        if (!local_inner_classes_.emplace(declaration.name, std::move(symbol)).second) {
            diagnostics_.error("GDS4101", "duplicate internal class '" + declaration.name + "'",
                               declaration.span);
        }
    }
    for (const auto& declaration : script.classes) {
        auto found = local_inner_classes_.find(declaration.name);
        if (found == local_inner_classes_.end())
            continue;
        for (const auto& variable : declaration.variables) {
            found->second.members.push_back(
                {variable.is_constant ? ScriptMemberKind::constant : ScriptMemberKind::field,
                 variable.name,
                 variable.type ? type_from_name(*variable.type, variable.span) : variant_type,
                 {},
                 0,
                 variable.is_constant || variable.is_static,
                 variable.getter.has_value() || variable.setter.has_value(),
                 false,
                 {},
                 {}});
        }
        for (const auto& function : declaration.functions) {
            ScriptMemberSymbol member;
            member.kind = ScriptMemberKind::function;
            member.name = function.name;
            member.type = function.name == "_init" ? void_type
                          : function.return_type
                              ? type_from_name(*function.return_type, function.span)
                              : variant_type;
            member.is_static = function.is_static;
            member.has_explicit_type = function.name == "_init" || function.return_type.has_value();
            for (const auto& parameter : function.parameters) {
                member.parameters.push_back(parameter.type
                                                ? type_from_name(*parameter.type, parameter.span)
                                                : variant_type);
                member.explicit_parameter_types.push_back(parameter.type.has_value());
                member.default_parameters.push_back(parameter.default_value != nullptr);
                if (!parameter.default_value)
                    ++member.required_arguments;
            }
            found->second.members.push_back(std::move(member));
        }
        for (const auto& signal : declaration.signals) {
            ScriptMemberSymbol member;
            member.kind = ScriptMemberKind::signal;
            member.name = signal.name;
            member.type = {TypeKind::builtin, "Signal"};
            for (const auto& parameter : signal.parameters) {
                member.parameters.push_back(parameter.type
                                                ? type_from_name(*parameter.type, parameter.span)
                                                : variant_type);
                ++member.required_arguments;
            }
            found->second.members.push_back(std::move(member));
        }
    }
    if (const auto local_base = local_inner_classes_.find(declared_base);
        local_base != local_inner_classes_.end()) {
        for (const auto& member : local_base->second.members) {
            auto kind = SymbolKind::field;
            if (member.kind == ScriptMemberKind::constant)
                kind = SymbolKind::constant;
            else if (member.kind == ScriptMemberKind::enum_value)
                kind = SymbolKind::enum_value;
            else if (member.kind == ScriptMemberKind::function)
                kind = SymbolKind::function;
            else if (member.kind == ScriptMemberKind::signal)
                kind = SymbolKind::signal;
            scopes_.front().insert_or_assign(
                member.name, Symbol{kind,
                                    member.name,
                                    member.type,
                                    {},
                                    member.kind == ScriptMemberKind::constant ||
                                        member.kind == ScriptMemberKind::enum_value});
            if (member.kind == ScriptMemberKind::field && member.has_accessor)
                accessor_fields_.insert(member.name);
            if (member.kind == ScriptMemberKind::field && member.is_static)
                static_fields_.insert(member.name);
        }
    }
    analyze_enums(script.enums);

    for (const auto& function : script.functions)
        functions_.emplace(function.name, &function);

    for (const auto& variable : script.variables) {
        if (variable.is_static)
            static_fields_.insert(variable.name);
        if (variable.getter || variable.setter) {
            accessor_fields_.insert(variable.name);
            if (variable.getter && !variable.getter->method.empty())
                bound_accessor_fields_[variable.getter->method].insert(variable.name);
            if (variable.setter && !variable.setter->method.empty())
                bound_accessor_fields_[variable.setter->method].insert(variable.name);
        }
    }

    for (const auto& variable : script.variables) {
        Type type = variable.type.has_value() ? type_from_name(*variable.type, variable.span)
                                              : variant_type;
        if (variable.infer_type || (variable.is_constant && !variable.type.has_value())) {
            type = unknown_type;
        }
        declare({variable.is_constant ? SymbolKind::constant : SymbolKind::field, variable.name,
                 type, variable.span, variable.is_constant});
    }
    for (const auto& signal : script.signals) {
        declare(
            {SymbolKind::signal, signal.name, {TypeKind::builtin, "Signal"}, signal.span, true});
    }
    for (const auto& function : script.functions) {
        const auto type = function.name == "_init" ? void_type
                          : function.return_type.has_value()
                              ? type_from_name(*function.return_type, function.span)
                              : variant_type;
        declare({SymbolKind::function, function.name, type, function.span, true});
    }

    for (const auto& declaration : script.classes)
        analyze_class(declaration);

    const auto analyze_script_variable = [&](const ast::VariableDeclaration& variable) {
        const auto initializer =
            variable.initializer ? analyze_expression(*variable.initializer) : variant_type;
        Type type = variable.type.has_value() ? type_from_name(*variable.type, variable.span)
                                              : variant_type;
        if (variable.infer_type || (variable.is_constant && !variable.type.has_value())) {
            type = initializer;
        } else if (variable.type.has_value() && variable.initializer) {
            require_assignable(type, initializer, variable.span, "invalid field initializer");
        }
        model_.variable_types_[&variable] = type;
        const auto implied_property = !variable.type && !variable.infer_type
                                          ? implied_export_property_type(variable)
                                          : std::nullopt;
        const auto property_type = implied_property ? *implied_property
                                   : has_property_annotation(variable) && !variable.type &&
                                           !variable.infer_type && variable.initializer
                                       ? initializer
                                       : type;
        model_.property_types_[&variable] = property_type;
        validate_annotations(variable, property_type);
        const auto found = scopes_.back().find(variable.name);
        if (found != scopes_.back().end())
            found->second.type = type;
    };
    for (const auto& variable : script.variables) {
        if (variable.is_constant)
            analyze_script_variable(variable);
    }
    for (const auto& variable : script.variables) {
        if (!variable.is_constant)
            analyze_script_variable(variable);
    }

    for (const auto& signal : script.signals) {
        Scope parameters;
        for (const auto& parameter : signal.parameters) {
            const auto type = parameter.type.has_value()
                                  ? type_from_name(*parameter.type, parameter.span)
                                  : variant_type;
            model_.parameter_types_[&parameter] = type;
            if (!parameters
                     .emplace(parameter.name, Symbol{SymbolKind::parameter, parameter.name, type,
                                                     parameter.span, true})
                     .second) {
                diagnostics_.error("GDS4010", "duplicate signal parameter '" + parameter.name + "'",
                                   parameter.span);
            }
        }
    }
    for (const auto& variable : script.variables)
        analyze_property_accessors(variable, model_.type_of(variable));
    for (const auto& function : script.functions)
        analyze_function(function);
    return std::move(model_);
}

} // namespace gdpp
