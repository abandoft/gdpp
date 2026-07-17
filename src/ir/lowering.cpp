#include "gdpp/ir/lowering.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace gdpp {
namespace {

ir::ExpressionKind lower_expression_kind(ast::ExpressionKind kind) {
    switch (kind) {
    case ast::ExpressionKind::literal:
        return ir::ExpressionKind::literal;
    case ast::ExpressionKind::identifier:
        return ir::ExpressionKind::identifier;
    case ast::ExpressionKind::unary:
        return ir::ExpressionKind::unary;
    case ast::ExpressionKind::binary:
        return ir::ExpressionKind::binary;
    case ast::ExpressionKind::call:
        return ir::ExpressionKind::call;
    case ast::ExpressionKind::member:
        return ir::ExpressionKind::member;
    case ast::ExpressionKind::subscript:
        return ir::ExpressionKind::subscript;
    case ast::ExpressionKind::conditional:
        return ir::ExpressionKind::conditional;
    case ast::ExpressionKind::node_reference:
        return ir::ExpressionKind::node_reference;
    case ast::ExpressionKind::array_literal:
        return ir::ExpressionKind::array_literal;
    case ast::ExpressionKind::dictionary_literal:
        return ir::ExpressionKind::dictionary_literal;
    case ast::ExpressionKind::lambda:
        return ir::ExpressionKind::lambda;
    }
    return ir::ExpressionKind::literal;
}

ir::LiteralKind lower_literal_kind(ast::LiteralKind kind) {
    switch (kind) {
    case ast::LiteralKind::none:
        return ir::LiteralKind::none;
    case ast::LiteralKind::nil:
        return ir::LiteralKind::nil;
    case ast::LiteralKind::boolean:
        return ir::LiteralKind::boolean;
    case ast::LiteralKind::integer:
        return ir::LiteralKind::integer;
    case ast::LiteralKind::floating:
        return ir::LiteralKind::floating;
    case ast::LiteralKind::string:
        return ir::LiteralKind::string;
    case ast::LiteralKind::string_name:
        return ir::LiteralKind::string_name;
    case ast::LiteralKind::node_path:
        return ir::LiteralKind::node_path;
    }
    return ir::LiteralKind::none;
}

ir::StatementKind lower_statement_kind(ast::StatementKind kind) {
    switch (kind) {
    case ast::StatementKind::expression:
        return ir::StatementKind::expression;
    case ast::StatementKind::return_statement:
        return ir::StatementKind::return_statement;
    case ast::StatementKind::await_statement:
        return ir::StatementKind::await_statement;
    case ast::StatementKind::await_variable:
        return ir::StatementKind::await_variable;
    case ast::StatementKind::assert_statement:
        return ir::StatementKind::assert_statement;
    case ast::StatementKind::variable:
        return ir::StatementKind::variable;
    case ast::StatementKind::assignment:
        return ir::StatementKind::assignment;
    case ast::StatementKind::if_statement:
        return ir::StatementKind::if_statement;
    case ast::StatementKind::match_statement:
        return ir::StatementKind::match_statement;
    case ast::StatementKind::while_statement:
        return ir::StatementKind::while_statement;
    case ast::StatementKind::for_statement:
        return ir::StatementKind::for_statement;
    case ast::StatementKind::pass_statement:
        return ir::StatementKind::pass_statement;
    case ast::StatementKind::break_statement:
        return ir::StatementKind::break_statement;
    case ast::StatementKind::continue_statement:
        return ir::StatementKind::continue_statement;
    }
    return ir::StatementKind::pass_statement;
}

} // namespace

ir::ExpressionPtr IrLowerer::lower_expression(const ast::Expression& expression) const {
    auto lowered = std::make_unique<ir::Expression>();
    lowered->kind = lower_expression_kind(expression.kind());
    lowered->literal_kind = lower_literal_kind(expression.literal_kind());
    lowered->type = semantic_.type_of(expression);
    lowered->span = expression.span;
    lowered->value = expression.value();
    if (const auto* resolution = semantic_.api_resolution_of(expression)) {
        if (resolution->kind == ApiResolutionKind::method)
            lowered->resolution = ir::ResolutionKind::godot_method;
        else if (resolution->kind == ApiResolutionKind::property)
            lowered->resolution = ir::ResolutionKind::godot_property;
        else if (resolution->kind == ApiResolutionKind::constructor)
            lowered->resolution = ir::ResolutionKind::godot_constructor;
        else if (resolution->kind == ApiResolutionKind::singleton)
            lowered->resolution = ir::ResolutionKind::godot_singleton;
        else if (resolution->kind == ApiResolutionKind::external_singleton)
            lowered->resolution = ir::ResolutionKind::external_singleton;
        else if (resolution->kind == ApiResolutionKind::type_reference)
            lowered->resolution = ir::ResolutionKind::godot_type;
        else if (resolution->kind == ApiResolutionKind::external_type_reference)
            lowered->resolution = ir::ResolutionKind::external_type;
        else if (resolution->kind == ApiResolutionKind::script_type_reference)
            lowered->resolution = ir::ResolutionKind::script_type;
        else if (resolution->kind == ApiResolutionKind::script_autoload)
            lowered->resolution = ir::ResolutionKind::script_autoload;
        else if (resolution->kind == ApiResolutionKind::script_constant)
            lowered->resolution = ir::ResolutionKind::script_constant;
        else if (resolution->kind == ApiResolutionKind::script_enum_type)
            lowered->resolution = ir::ResolutionKind::script_enum_type;
        else if (resolution->kind == ApiResolutionKind::script_resource)
            lowered->resolution = ir::ResolutionKind::script_resource;
        else if (resolution->kind == ApiResolutionKind::script_constructor)
            lowered->resolution = ir::ResolutionKind::script_constructor;
        else if (resolution->kind == ApiResolutionKind::external_constructor)
            lowered->resolution = ir::ResolutionKind::external_constructor;
        else if (resolution->kind == ApiResolutionKind::external_static_method)
            lowered->resolution = ir::ResolutionKind::external_static_method;
        else if (resolution->kind == ApiResolutionKind::external_callable)
            lowered->resolution = ir::ResolutionKind::external_callable;
        else if (resolution->kind == ApiResolutionKind::external_signal)
            lowered->resolution = ir::ResolutionKind::external_signal;
        else if (resolution->kind == ApiResolutionKind::inner_constructor)
            lowered->resolution = ir::ResolutionKind::inner_constructor;
        else if (resolution->kind == ApiResolutionKind::inner_type_reference)
            lowered->resolution = ir::ResolutionKind::inner_type;
        else if (resolution->kind == ApiResolutionKind::script_super)
            lowered->resolution = ir::ResolutionKind::script_super;
        else if (resolution->kind == ApiResolutionKind::script_signal)
            lowered->resolution = ir::ResolutionKind::script_signal;
        else if (resolution->kind == ApiResolutionKind::script_callable)
            lowered->resolution = ir::ResolutionKind::script_callable;
        else if (resolution->kind == ApiResolutionKind::script_static_callable)
            lowered->resolution = ir::ResolutionKind::script_static_callable;
        else if (resolution->kind == ApiResolutionKind::script_static_field)
            lowered->resolution = ir::ResolutionKind::script_static_field;
        else if (resolution->kind == ApiResolutionKind::script_free)
            lowered->resolution = ir::ResolutionKind::script_free;
        else if (resolution->kind == ApiResolutionKind::enum_member)
            lowered->resolution = ir::ResolutionKind::enum_member;
        else if (resolution->kind == ApiResolutionKind::script_property)
            lowered->resolution = ir::ResolutionKind::script_property;
        else if (resolution->kind == ApiResolutionKind::dynamic_method)
            lowered->resolution = ir::ResolutionKind::dynamic_method;
        else if (resolution->kind == ApiResolutionKind::dynamic_property)
            lowered->resolution = ir::ResolutionKind::dynamic_property;
        else if (resolution->kind == ApiResolutionKind::utility_function)
            lowered->resolution = ir::ResolutionKind::utility_function;
        else if (resolution->kind == ApiResolutionKind::global_constant)
            lowered->resolution = ir::ResolutionKind::global_constant;
        else if (resolution->kind == ApiResolutionKind::global_enum_type)
            lowered->resolution = ir::ResolutionKind::global_enum_type;
        else if (resolution->kind == ApiResolutionKind::global_enum_value)
            lowered->resolution = ir::ResolutionKind::global_enum_value;
        else if (resolution->kind == ApiResolutionKind::builtin_constant)
            lowered->resolution = ir::ResolutionKind::builtin_constant;
        else if (resolution->kind == ApiResolutionKind::intrinsic)
            lowered->resolution = ir::ResolutionKind::intrinsic;
        if (resolution->kind == ApiResolutionKind::constructor)
            lowered->type = resolution->type;
        lowered->resolved_owner = resolution->owner;
        lowered->getter = resolution->getter;
        lowered->setter = resolution->setter;
        lowered->direct_access = resolution->direct;
        lowered->indexed_argument = resolution->indexed_argument;
        lowered->intrinsic = resolution->intrinsic;
    }
    lowered->operands.reserve(expression.operand_count());
    for (std::size_t index = 0; index < expression.operand_count(); ++index)
        lowered->operands.push_back(lower_expression(*expression.operand(index)));
    if (const auto* lambda = expression.lambda()) {
        lowered->lambda = std::make_unique<ir::LambdaExpression>();
        lowered->lambda->return_type = semantic_.return_type_of(*lambda);
        lowered->lambda->owner_bound = semantic_.owner_bound(*lambda);
        lowered->lambda->span = lambda->span;
        lowered->lambda->parameters.reserve(lambda->parameters.size());
        for (const auto& parameter : lambda->parameters)
            lowered->lambda->parameters.push_back(lower_parameter(parameter));
        lowered->lambda->body.reserve(lambda->body.size());
        for (const auto& statement : lambda->body)
            lowered->lambda->body.push_back(lower_statement(statement));
    }
    return lowered;
}

ir::Parameter IrLowerer::lower_parameter(const ast::Parameter& parameter) const {
    ir::Parameter lowered;
    lowered.name = parameter.name;
    lowered.type = semantic_.type_of(parameter);
    if (parameter.default_value)
        lowered.default_value = lower_expression(*parameter.default_value);
    lowered.span = parameter.span;
    return lowered;
}

ir::Statement IrLowerer::lower_statement(const ast::Statement& statement) const {
    ir::Statement lowered;
    lowered.kind = lower_statement_kind(statement.kind());
    lowered.span = statement.span;
    lowered.name = statement.name();
    lowered.declared_type = statement.kind() == ast::StatementKind::variable ||
                                    statement.kind() == ast::StatementKind::await_variable ||
                                    statement.kind() == ast::StatementKind::for_statement
                                ? semantic_.type_of(statement)
                                : Type{TypeKind::unknown, "unknown"};
    lowered.operation = statement.operation();
    if (statement.expression())
        lowered.expression = lower_expression(*statement.expression());
    if (statement.condition())
        lowered.condition = lower_expression(*statement.condition());
    lowered.body.reserve(statement.body().size());
    for (const auto& child : statement.body())
        lowered.body.push_back(lower_statement(child));
    lowered.else_body.reserve(statement.else_body().size());
    for (const auto& child : statement.else_body()) {
        lowered.else_body.push_back(lower_statement(child));
    }
    if (statement.kind() == ast::StatementKind::match_statement) {
        lowered.body.reserve(statement.match_branches().size());
        for (const auto& branch : statement.match_branches()) {
            ir::Statement lowered_branch;
            lowered_branch.kind = ir::StatementKind::match_branch;
            lowered_branch.span = branch.span;
            if (branch.guard)
                lowered_branch.expression = lower_expression(*branch.guard);
            for (const auto& child : branch.body)
                lowered_branch.body.push_back(lower_statement(child));
            for (const auto& pattern : branch.patterns) {
                ir::MatchPattern lowered_pattern;
                if (pattern.kind() == ast::MatchPatternKind::wildcard)
                    lowered_pattern.kind = ir::MatchPatternKind::wildcard;
                else if (pattern.kind() == ast::MatchPatternKind::binding)
                    lowered_pattern.kind = ir::MatchPatternKind::binding;
                lowered_pattern.name = pattern.name();
                lowered_pattern.span = pattern.span;
                if (pattern.expression())
                    lowered_pattern.expression = lower_expression(*pattern.expression());
                lowered_branch.patterns.push_back(std::move(lowered_pattern));
            }
            lowered.body.push_back(std::move(lowered_branch));
        }
    }
    return lowered;
}

ir::Class IrLowerer::lower_class(const ast::ClassDeclaration& declaration) const {
    ir::Class lowered;
    lowered.name = declaration.name;
    lowered.base_type = declaration.base_type.value_or("RefCounted");
    lowered.span = declaration.span;
    for (const auto& enumeration : declaration.enums) {
        ir::Enum result;
        result.name = enumeration.name.value_or("");
        result.span = enumeration.span;
        for (const auto& entry : enumeration.entries)
            result.entries.push_back({entry.name, semantic_.value_of(entry)});
        lowered.enums.push_back(std::move(result));
    }
    for (const auto& variable : declaration.variables) {
        ir::Field field;
        field.name = variable.name;
        field.type = semantic_.type_of(variable);
        field.property_type = semantic_.property_type_of(variable);
        field.is_constant = variable.is_constant;
        field.is_static = variable.is_static;
        field.onready = variable.onready;
        field.span = variable.span;
        if (variable.initializer)
            field.initializer = lower_expression(*variable.initializer);
        if (variable.getter) {
            ir::PropertyAccessor accessor;
            accessor.method = variable.getter->method;
            accessor.span = variable.getter->span;
            for (const auto& statement : variable.getter->body)
                accessor.body.push_back(lower_statement(statement));
            field.getter = std::move(accessor);
        }
        if (variable.setter) {
            ir::PropertyAccessor accessor;
            accessor.parameter = variable.setter->parameter;
            accessor.method = variable.setter->method;
            accessor.span = variable.setter->span;
            for (const auto& statement : variable.setter->body)
                accessor.body.push_back(lower_statement(statement));
            field.setter = std::move(accessor);
        }
        lowered.fields.push_back(std::move(field));
    }
    for (const auto& signal : declaration.signals) {
        ir::Signal result;
        result.name = signal.name;
        result.span = signal.span;
        for (const auto& parameter : signal.parameters)
            result.parameters.push_back(lower_parameter(parameter));
        lowered.signals.push_back(std::move(result));
    }
    for (const auto& function : declaration.functions) {
        ir::Function result;
        result.name = function.name;
        result.return_type = semantic_.return_type_of(function);
        result.is_static = function.is_static;
        result.span = function.span;
        for (const auto& parameter : function.parameters)
            result.parameters.push_back(lower_parameter(parameter));
        for (const auto& statement : function.body)
            result.body.push_back(lower_statement(statement));
        lowered.functions.push_back(std::move(result));
    }
    for (const auto& inner : declaration.classes)
        lowered.classes.push_back(lower_class(inner));
    return lowered;
}

ir::Module IrLowerer::lower(const ast::Script& script) const {
    ir::Module module;
    module.base_type = script.base_type;
    module.class_name = script.class_name;
    module.is_abstract = std::any_of(
        script.annotations.begin(), script.annotations.end(),
        [](const ast::Annotation& annotation) { return annotation.name == "abstract"; });
    module.span = script.span;
    module.enums.reserve(script.enums.size());
    for (const auto& declaration : script.enums) {
        ir::Enum enumeration;
        enumeration.name = declaration.name.value_or("");
        enumeration.span = declaration.span;
        enumeration.entries.reserve(declaration.entries.size());
        for (const auto& entry : declaration.entries) {
            enumeration.entries.push_back({entry.name, semantic_.value_of(entry)});
        }
        module.enums.push_back(std::move(enumeration));
    }
    module.fields.reserve(script.variables.size());
    for (const auto& variable : script.variables) {
        ir::Field field;
        field.name = variable.name;
        field.type = semantic_.type_of(variable);
        field.property_type = semantic_.property_type_of(variable);
        field.is_static = variable.is_static;
        field.onready = variable.onready;
        if (variable.initializer)
            field.initializer = lower_expression(*variable.initializer);
        if (variable.getter) {
            ir::PropertyAccessor getter;
            getter.method = variable.getter->method;
            getter.span = variable.getter->span;
            getter.body.reserve(variable.getter->body.size());
            for (const auto& statement : variable.getter->body)
                getter.body.push_back(lower_statement(statement));
            field.getter = std::move(getter);
        }
        if (variable.setter) {
            ir::PropertyAccessor setter;
            setter.parameter = variable.setter->parameter;
            setter.method = variable.setter->method;
            setter.span = variable.setter->span;
            setter.body.reserve(variable.setter->body.size());
            for (const auto& statement : variable.setter->body)
                setter.body.push_back(lower_statement(statement));
            field.setter = std::move(setter);
        }
        for (const auto& annotation : variable.annotations) {
            if (annotation.name == "onready" || annotation.name == "warning_ignore")
                continue;
            ir::PropertyAnnotation property;
            property.name = annotation.name;
            for (const auto& argument : annotation.arguments) {
                ir::PropertyArgument lowered;
                const ast::Expression* value = argument.get();
                std::string prefix;
                if (const auto* unary = value->get_if<ast::UnaryExpression>();
                    unary && (unary->operation == "+" || unary->operation == "-")) {
                    prefix = unary->operation;
                    value = unary->operand.get();
                }
                lowered.kind = value->literal_kind() == ast::LiteralKind::string
                                   ? ir::PropertyArgumentKind::string
                                   : ir::PropertyArgumentKind::number;
                lowered.value = prefix + value->value();
                property.arguments.push_back(std::move(lowered));
            }
            if (annotation.name == "export_group" || annotation.name == "export_subgroup" ||
                annotation.name == "export_category")
                field.property_groups.push_back(std::move(property));
            else
                field.property = std::move(property);
        }
        if (field.property && field.type.kind == TypeKind::enumeration) {
            for (const auto& enumeration : module.enums) {
                const auto separator = field.type.name.rfind('.');
                const auto local_name = separator == std::string::npos
                                            ? field.type.name
                                            : field.type.name.substr(separator + 1);
                if (enumeration.name != local_name)
                    continue;
                for (const auto& entry : enumeration.entries) {
                    if (!field.enum_hint.empty())
                        field.enum_hint.push_back(',');
                    field.enum_hint += entry.name + ":" + std::to_string(entry.value);
                }
                break;
            }
        }
        field.is_constant = variable.is_constant;
        field.span = variable.span;
        module.fields.push_back(std::move(field));
    }
    module.signals.reserve(script.signals.size());
    for (const auto& signal : script.signals) {
        ir::Signal lowered;
        lowered.name = signal.name;
        lowered.span = signal.span;
        lowered.parameters.reserve(signal.parameters.size());
        for (const auto& parameter : signal.parameters) {
            lowered.parameters.push_back(lower_parameter(parameter));
        }
        module.signals.push_back(std::move(lowered));
    }
    module.functions.reserve(script.functions.size());
    for (const auto& function : script.functions) {
        ir::Function lowered;
        lowered.name = function.name;
        lowered.return_type = semantic_.return_type_of(function);
        lowered.is_static = function.is_static;
        lowered.span = function.span;
        lowered.parameters.reserve(function.parameters.size());
        for (const auto& parameter : function.parameters) {
            lowered.parameters.push_back(lower_parameter(parameter));
        }
        lowered.body.reserve(function.body.size());
        for (const auto& statement : function.body) {
            lowered.body.push_back(lower_statement(statement));
        }
        module.functions.push_back(std::move(lowered));
    }
    module.classes.reserve(script.classes.size());
    for (const auto& declaration : script.classes)
        module.classes.push_back(lower_class(declaration));
    return module;
}

bool IrVerifier::verify_expression(const ir::Expression& expression) {
    std::size_t minimum = 0;
    std::optional<std::size_t> exact;
    switch (expression.kind) {
    case ir::ExpressionKind::literal:
    case ir::ExpressionKind::identifier:
    case ir::ExpressionKind::node_reference:
    case ir::ExpressionKind::array_literal:
        break;
    case ir::ExpressionKind::lambda:
        exact = 0;
        break;
    case ir::ExpressionKind::unary:
    case ir::ExpressionKind::member:
        exact = 1;
        break;
    case ir::ExpressionKind::binary:
    case ir::ExpressionKind::subscript:
        exact = 2;
        break;
    case ir::ExpressionKind::conditional:
        exact = 3;
        break;
    case ir::ExpressionKind::call:
        minimum = 1;
        break;
    case ir::ExpressionKind::dictionary_literal:
        if (expression.operands.size() % 2 != 0) {
            diagnostics_.error("GDS5001", "dictionary IR must contain key/value operand pairs",
                               expression.span);
            return false;
        }
        break;
    }
    bool valid = true;
    if ((exact.has_value() && expression.operands.size() != *exact) ||
        expression.operands.size() < minimum) {
        diagnostics_.error("GDS5002", "invalid operand count in typed IR", expression.span);
        valid = false;
    }
    if (expression.kind == ir::ExpressionKind::literal &&
        expression.literal_kind == ir::LiteralKind::none) {
        diagnostics_.error("GDS5003", "literal IR is missing its literal type", expression.span);
        valid = false;
    }
    for (const auto& operand : expression.operands) {
        if (!operand || !verify_expression(*operand))
            valid = false;
    }
    if (expression.kind == ir::ExpressionKind::lambda) {
        if (!expression.lambda) {
            diagnostics_.error("GDS5026", "lambda IR is missing its function payload",
                               expression.span);
            valid = false;
        } else {
            for (const auto& parameter : expression.lambda->parameters) {
                if (parameter.name.empty()) {
                    diagnostics_.error("GDS5027", "lambda parameter is missing its name",
                                       parameter.span);
                    valid = false;
                }
                if (parameter.default_value && !verify_expression(*parameter.default_value))
                    valid = false;
            }
            for (const auto& statement : expression.lambda->body) {
                if (!verify_statement(statement))
                    valid = false;
            }
        }
    }
    if (expression.kind == ir::ExpressionKind::binary &&
        (expression.value == "is" || expression.value == "is not") &&
        expression.operands.size() == 2) {
        const auto resolution = expression.operands[1]->resolution;
        if (resolution != ir::ResolutionKind::godot_type &&
            resolution != ir::ResolutionKind::external_type &&
            resolution != ir::ResolutionKind::script_type &&
            resolution != ir::ResolutionKind::inner_type) {
            diagnostics_.error("GDS5023", "type-test IR requires a resolved type operand",
                               expression.span);
            valid = false;
        }
    }
    if (expression.kind == ir::ExpressionKind::member &&
        (expression.resolution == ir::ResolutionKind::dynamic_method ||
         expression.resolution == ir::ResolutionKind::dynamic_property)) {
        const bool valid_receiver =
            expression.operands.size() == 1 &&
            (expression.operands.front()->type.is_dynamic() ||
             expression.operands.front()->type.kind == TypeKind::object ||
             (expression.resolution == ir::ResolutionKind::dynamic_property &&
              expression.operands.front()->type.kind == TypeKind::dictionary));
        const bool contract_typed = !expression.resolved_owner.empty();
        if (!valid_receiver || (!expression.type.is_dynamic() && !contract_typed)) {
            diagnostics_.error("GDS5024",
                               "dynamic member IR requires an object-compatible receiver and "
                               "a Variant or bridge-contract result",
                               expression.span);
            valid = false;
        }
    }
    return valid;
}

bool IrVerifier::verify_statement(const ir::Statement& statement) {
    bool valid = true;
    const auto require_expression = [&] {
        if (!statement.expression) {
            diagnostics_.error("GDS5004", "statement IR is missing an expression", statement.span);
            valid = false;
        }
    };
    const auto require_condition = [&] {
        if (!statement.condition) {
            diagnostics_.error("GDS5005", "control-flow IR is missing a condition", statement.span);
            valid = false;
        }
    };
    switch (statement.kind) {
    case ir::StatementKind::expression:
    case ir::StatementKind::assignment:
        require_expression();
        break;
    case ir::StatementKind::variable:
    case ir::StatementKind::await_variable:
        if (statement.name.empty()) {
            diagnostics_.error("GDS5006", "variable IR is missing a name", statement.span);
            valid = false;
        }
        break;
    case ir::StatementKind::if_statement:
    case ir::StatementKind::while_statement:
    case ir::StatementKind::for_statement:
        require_condition();
        break;
    case ir::StatementKind::match_statement:
        require_condition();
        if (statement.body.empty()) {
            diagnostics_.error("GDS5016", "match IR is missing branches", statement.span);
            valid = false;
        }
        break;
    case ir::StatementKind::match_branch:
        if (statement.patterns.empty()) {
            diagnostics_.error("GDS5017", "match branch IR is missing patterns", statement.span);
            valid = false;
        }
        break;
    case ir::StatementKind::return_statement:
    case ir::StatementKind::await_statement:
    case ir::StatementKind::assert_statement:
    case ir::StatementKind::pass_statement:
    case ir::StatementKind::break_statement:
    case ir::StatementKind::continue_statement:
        break;
    }
    if (statement.kind == ir::StatementKind::assignment && !statement.condition) {
        diagnostics_.error("GDS5007", "assignment IR is missing its target", statement.span);
        valid = false;
    }
    if (statement.kind == ir::StatementKind::assignment && statement.condition &&
        statement.condition->kind != ir::ExpressionKind::identifier &&
        statement.condition->kind != ir::ExpressionKind::member &&
        statement.condition->kind != ir::ExpressionKind::subscript) {
        diagnostics_.error("GDS5029", "assignment IR target is not an lvalue",
                           statement.condition->span);
        valid = false;
    }
    if (statement.kind == ir::StatementKind::assert_statement && !statement.condition) {
        diagnostics_.error("GDS5022", "assert IR is missing its condition", statement.span);
        valid = false;
    }
    if ((statement.kind == ir::StatementKind::await_statement ||
         statement.kind == ir::StatementKind::await_variable) &&
        !statement.expression) {
        diagnostics_.error("GDS5025", "await IR is missing its signal expression", statement.span);
        valid = false;
    }
    if (statement.expression && !verify_expression(*statement.expression))
        valid = false;
    if (statement.condition && !verify_expression(*statement.condition))
        valid = false;
    for (const auto& pattern : statement.patterns) {
        if (pattern.kind == ir::MatchPatternKind::value &&
            (!pattern.expression || !verify_expression(*pattern.expression))) {
            diagnostics_.error("GDS5018", "match value pattern IR is invalid", pattern.span);
            valid = false;
        }
        if (pattern.kind == ir::MatchPatternKind::binding && pattern.name.empty()) {
            diagnostics_.error("GDS5019", "match binding IR is missing a name", pattern.span);
            valid = false;
        }
    }
    for (const auto& child : statement.body) {
        if (!verify_statement(child))
            valid = false;
    }
    for (const auto& child : statement.else_body) {
        if (!verify_statement(child))
            valid = false;
    }
    return valid;
}

bool IrVerifier::verify_class(const ir::Class& declaration) {
    bool valid = !declaration.name.empty();
    if (!valid)
        diagnostics_.error("GDS5028", "internal class IR is missing its name", declaration.span);
    for (const auto& field : declaration.fields) {
        if (field.name.empty()) {
            diagnostics_.error("GDS5008", "internal field IR is missing a name", field.span);
            valid = false;
        }
        if (field.initializer && !verify_expression(*field.initializer))
            valid = false;
        if (field.getter) {
            for (const auto& statement : field.getter->body)
                valid = verify_statement(statement) && valid;
        }
        if (field.setter) {
            for (const auto& statement : field.setter->body)
                valid = verify_statement(statement) && valid;
        }
    }
    for (const auto& function : declaration.functions) {
        if (function.name.empty()) {
            diagnostics_.error("GDS5011", "internal function IR is missing a name", function.span);
            valid = false;
        }
        for (const auto& parameter : function.parameters) {
            if (parameter.default_value)
                valid = verify_expression(*parameter.default_value) && valid;
        }
        for (const auto& statement : function.body)
            valid = verify_statement(statement) && valid;
    }
    for (const auto& inner : declaration.classes)
        valid = verify_class(inner) && valid;
    return valid;
}

bool IrVerifier::verify(const ir::Module& module) {
    bool valid = true;
    for (const auto& enumeration : module.enums) {
        if (enumeration.entries.empty()) {
            diagnostics_.error("GDS5014", "enum IR must contain at least one member",
                               enumeration.span);
            valid = false;
        }
        std::unordered_set<std::string> names;
        for (const auto& entry : enumeration.entries) {
            if (entry.name.empty() || !names.insert(entry.name).second) {
                diagnostics_.error("GDS5015", "enum IR contains an invalid member name",
                                   enumeration.span);
                valid = false;
            }
        }
    }
    for (const auto& field : module.fields) {
        if (field.name.empty()) {
            diagnostics_.error("GDS5008", "field IR is missing a name", field.span);
            valid = false;
        }
        if (field.is_constant && !field.initializer) {
            diagnostics_.error("GDS5009", "constant IR is missing an initializer", field.span);
            valid = false;
        }
        if (field.property && field.is_constant) {
            diagnostics_.error("GDS5012", "export property IR cannot describe a constant",
                               field.span);
            valid = false;
        }
        if ((field.getter || field.setter) && field.is_constant) {
            diagnostics_.error("GDS5020", "property accessor IR cannot describe a constant",
                               field.span);
            valid = false;
        }
        if (field.property && field.property->name.empty()) {
            diagnostics_.error("GDS5013", "export property IR is missing its annotation",
                               field.span);
            valid = false;
        }
        if (field.initializer && !verify_expression(*field.initializer))
            valid = false;
        if (field.getter) {
            for (const auto& statement : field.getter->body) {
                if (!verify_statement(statement))
                    valid = false;
            }
        }
        if (field.setter) {
            if (field.setter->parameter.empty() && field.setter->method.empty()) {
                diagnostics_.error("GDS5021", "setter IR is missing its parameter", field.span);
                valid = false;
            }
            for (const auto& statement : field.setter->body) {
                if (!verify_statement(statement))
                    valid = false;
            }
        }
    }
    for (const auto& signal : module.signals) {
        if (signal.name.empty()) {
            diagnostics_.error("GDS5010", "signal IR is missing a name", signal.span);
            valid = false;
        }
        for (const auto& parameter : signal.parameters) {
            if (parameter.default_value && !verify_expression(*parameter.default_value)) {
                valid = false;
            }
        }
    }
    for (const auto& function : module.functions) {
        if (function.name.empty()) {
            diagnostics_.error("GDS5011", "function IR is missing a name", function.span);
            valid = false;
        }
        for (const auto& parameter : function.parameters) {
            if (parameter.default_value && !verify_expression(*parameter.default_value)) {
                valid = false;
            }
        }
        for (const auto& statement : function.body) {
            if (!verify_statement(statement))
                valid = false;
        }
    }
    for (const auto& declaration : module.classes)
        valid = verify_class(declaration) && valid;
    return valid;
}

} // namespace gdpp
