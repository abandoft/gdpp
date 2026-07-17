#include "support/test.hpp"

#include "gdpp/diagnostic.hpp"
#include "gdpp/ir_lowerer.hpp"
#include "gdpp/ir_optimizer.hpp"
#include "gdpp/lexer.hpp"
#include "gdpp/parser.hpp"
#include "gdpp/semantic.hpp"
#include "gdpp/source.hpp"

namespace {

gdpp::ir::Module lower_source(const std::string& text, gdpp::DiagnosticBag& diagnostics) {
    const gdpp::SourceFile source{"ir_test.gd", text};
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();
    gdpp::Parser parser{tokens, diagnostics};
    const auto script = parser.parse_script();
    gdpp::SemanticAnalyzer analyzer{diagnostics};
    const auto semantic = analyzer.analyze(script);
    const gdpp::IrLowerer lowerer{semantic};
    return lowerer.lower(script);
}

} // namespace

TEST_CASE("typed IR owns resolved declaration and expression types") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("var answer := 40 + 2\n", diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.fields.size(), std::size_t{1});
    REQUIRE_EQ(module.fields.front().type.kind, gdpp::TypeKind::integer);
    REQUIRE_EQ(module.fields.front().initializer->type.kind, gdpp::TypeKind::integer);
    REQUIRE_EQ(module.fields.front().initializer->kind, gdpp::ir::ExpressionKind::binary);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR preserves dynamic method and property dispatch") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("func invoke(target: Variant) -> Variant:\n"
                                     "    return target.answer()\n"
                                     "func read(target: Variant) -> Variant:\n"
                                     "    return target.score\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.functions.size(), std::size_t{2});
    const auto& call = *module.functions[0].body.front().expression;
    REQUIRE_EQ(call.kind, gdpp::ir::ExpressionKind::call);
    REQUIRE_EQ(call.operands.front()->resolution, gdpp::ir::ResolutionKind::dynamic_method);
    const auto& property = *module.functions[1].body.front().expression;
    REQUIRE_EQ(property.kind, gdpp::ir::ExpressionKind::member);
    REQUIRE_EQ(property.resolution, gdpp::ir::ResolutionKind::dynamic_property);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR preserves internal classes and owner-bound lambdas") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("class Payload:\n"
                                     "    var value: int\n"
                                     "    func _init(initial: int) -> void:\n"
                                     "        value = initial\n"
                                     "signal changed(value: int)\n"
                                     "func attach() -> void:\n"
                                     "    changed.connect(\n"
                                     "        func(value: int) -> void:\n"
                                     "            print(value))\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.classes.size(), std::size_t{1});
    REQUIRE_EQ(module.classes.front().name, std::string{"Payload"});
    const auto& call = *module.functions.front().body.front().expression;
    REQUIRE_EQ(call.operands.back()->kind, gdpp::ir::ExpressionKind::lambda);
    REQUIRE(call.operands.back()->lambda != nullptr);
    REQUIRE(call.operands.back()->lambda->owner_bound);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("IR optimizer folds constants and removes unreachable statements") {
    gdpp::DiagnosticBag diagnostics;
    auto module = lower_source("func answer() -> int:\n"
                               "    return 40 + 2\n",
                               diagnostics);
    REQUIRE(!diagnostics.has_errors());
    gdpp::ir::Statement unreachable;
    unreachable.kind = gdpp::ir::StatementKind::expression;
    unreachable.expression = std::make_unique<gdpp::ir::Expression>();
    unreachable.expression->kind = gdpp::ir::ExpressionKind::literal;
    unreachable.expression->literal_kind = gdpp::ir::LiteralKind::integer;
    unreachable.expression->type = {gdpp::TypeKind::integer, "int"};
    unreachable.expression->value = "7";
    module.functions.front().body.push_back(std::move(unreachable));

    const gdpp::IrOptimizer optimizer;
    const auto stats = optimizer.optimize(module);

    REQUIRE_EQ(stats.constants_folded, std::size_t{1});
    REQUIRE_EQ(stats.statements_removed, std::size_t{1});
    REQUIRE_EQ(module.functions.front().body.size(), std::size_t{1});
    const auto& value = *module.functions.front().body.front().expression;
    REQUIRE_EQ(value.kind, gdpp::ir::ExpressionKind::literal);
    REQUIRE_EQ(value.literal_kind, gdpp::ir::LiteralKind::integer);
    REQUIRE_EQ(value.value, std::string{"42"});
}

TEST_CASE("IR optimizer preserves exact int64 values above double precision") {
    gdpp::DiagnosticBag diagnostics;
    auto module = lower_source("func exact() -> int:\n"
                               "    return 9007199254740993 + 2\n",
                               diagnostics);
    REQUIRE(!diagnostics.has_errors());

    const auto stats = gdpp::IrOptimizer{}.optimize(module);

    REQUIRE_EQ(stats.constants_folded, std::size_t{1});
    const auto& value = *module.functions.front().body.front().expression;
    REQUIRE_EQ(value.literal_kind, gdpp::ir::LiteralKind::integer);
    REQUIRE_EQ(value.value, std::string{"9007199254740995"});
}

TEST_CASE("IR verifier rejects structurally invalid collection IR") {
    gdpp::ir::Module module;
    gdpp::ir::Field field;
    field.name = "broken";
    field.type = {gdpp::TypeKind::dictionary, "Dictionary"};
    field.initializer = std::make_unique<gdpp::ir::Expression>();
    field.initializer->kind = gdpp::ir::ExpressionKind::dictionary_literal;
    field.initializer->type = field.type;
    field.initializer->operands.push_back(std::make_unique<gdpp::ir::Expression>());
    module.fields.push_back(std::move(field));
    gdpp::DiagnosticBag diagnostics;
    gdpp::IrVerifier verifier{diagnostics};

    REQUIRE(!verifier.verify(module));
    REQUIRE(diagnostics.has_errors());
}

TEST_CASE("typed IR preserves validated export property metadata") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source(
        "@export_range(-5.0, 20.0, 0.25, \"or_greater\") var speed: float = 2.0\n", diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE(module.fields.front().property.has_value());
    REQUIRE_EQ(module.fields.front().property->name, std::string{"export_range"});
    REQUIRE_EQ(module.fields.front().property->arguments.size(), std::size_t{4});
    REQUIRE_EQ(module.fields.front().property->arguments.front().value, std::string{"-5.0"});
    REQUIRE_EQ(module.fields.front().property->arguments.back().kind,
               gdpp::ir::PropertyArgumentKind::string);
}

TEST_CASE("IR verifier rejects exported constants") {
    gdpp::ir::Module module;
    gdpp::ir::Field field;
    field.name = "invalid";
    field.type = {gdpp::TypeKind::integer, "int"};
    field.is_constant = true;
    field.property = gdpp::ir::PropertyAnnotation{"export", {}};
    field.initializer = std::make_unique<gdpp::ir::Expression>();
    field.initializer->kind = gdpp::ir::ExpressionKind::literal;
    field.initializer->literal_kind = gdpp::ir::LiteralKind::integer;
    field.initializer->type = field.type;
    field.initializer->value = "1";
    module.fields.push_back(std::move(field));
    gdpp::DiagnosticBag diagnostics;
    gdpp::IrVerifier verifier{diagnostics};

    REQUIRE(!verifier.verify(module));
    REQUIRE(diagnostics.has_errors());
}

TEST_CASE("typed IR owns evaluated enum values and enum export hints") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("enum State { IDLE, WALK = 4, RUN = WALK * 2 }\n"
                                     "@export var state: State = State.RUN\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.enums.size(), std::size_t{1});
    REQUIRE_EQ(module.enums.front().entries.at(0).value, std::int64_t{0});
    REQUIRE_EQ(module.enums.front().entries.at(1).value, std::int64_t{4});
    REQUIRE_EQ(module.enums.front().entries.at(2).value, std::int64_t{8});
    REQUIRE_EQ(module.fields.front().type.kind, gdpp::TypeKind::enumeration);
    REQUIRE_EQ(module.fields.front().enum_hint, std::string{"IDLE:0,WALK:4,RUN:8"});
}

TEST_CASE("typed IR preserves match patterns guards and branch scopes") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("func classify(value: int) -> String:\n"
                                     "    match value:\n"
                                     "        0, 1:\n"
                                     "            return \"small\"\n"
                                     "        var captured when captured > 7:\n"
                                     "            return \"large\"\n"
                                     "        _:\n"
                                     "            return \"other\"\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    const auto& match = module.functions.front().body.front();
    REQUIRE_EQ(match.kind, gdpp::ir::StatementKind::match_statement);
    REQUIRE_EQ(match.condition->type.kind, gdpp::TypeKind::integer);
    REQUIRE_EQ(match.body.at(1).patterns.front().kind, gdpp::ir::MatchPatternKind::binding);
    REQUIRE_EQ(match.body.at(1).expression->type.kind, gdpp::TypeKind::boolean);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR preserves assert condition and optional message") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("func validate(value: int) -> int:\n"
                                     "    assert(value > 0, \"positive value required\")\n"
                                     "    return value\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    const auto& assertion = module.functions.front().body.front();
    REQUIRE_EQ(assertion.kind, gdpp::ir::StatementKind::assert_statement);
    REQUIRE_EQ(assertion.condition->type.kind, gdpp::TypeKind::boolean);
    REQUIRE_EQ(assertion.expression->type.kind, gdpp::TypeKind::string);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR preserves signal await suspension points") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("signal resumed\n"
                                     "func run() -> void:\n"
                                     "    await resumed\n"
                                     "    pass\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.functions.front().body.front().kind,
               gdpp::ir::StatementKind::await_statement);
    REQUIRE_EQ(module.functions.front().body.front().expression->type.kind,
               gdpp::TypeKind::builtin);
    REQUIRE_EQ(module.functions.front().body.front().expression->type.name, std::string{"Signal"});
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR preserves a local initialized from an awaited signal") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("signal selected(value: String)\n"
                                     "func run():\n"
                                     "    var value = await selected\n"
                                     "    print(value)\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.functions.front().body.front().kind, gdpp::ir::StatementKind::await_variable);
    REQUIRE_EQ(module.functions.front().body.front().name, std::string{"value"});
    REQUIRE_EQ(module.functions.front().body.front().declared_type.kind, gdpp::TypeKind::variant);
    gdpp::IrVerifier verifier{diagnostics};
    REQUIRE(verifier.verify(module));
}

TEST_CASE("typed IR distinguishes property accessors from their backing field") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("var health: int = 100:\n"
                                     "    set(value):\n"
                                     "        health = value\n"
                                     "    get:\n"
                                     "        return health\n"
                                     "func update(value: int) -> int:\n"
                                     "    health = value\n"
                                     "    return health\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE(module.fields.front().getter.has_value());
    REQUIRE(module.fields.front().setter.has_value());
    const auto& setter_target = *module.fields.front().setter->body.front().condition;
    REQUIRE_EQ(setter_target.resolution, gdpp::ir::ResolutionKind::none);
    const auto& function_target = *module.functions.front().body.front().condition;
    REQUIRE_EQ(function_target.resolution, gdpp::ir::ResolutionKind::script_property);
    const auto& function_read = *module.functions.front().body.back().expression;
    REQUIRE_EQ(function_read.resolution, gdpp::ir::ResolutionKind::script_property);
}

TEST_CASE("typed IR preserves bound accessors and direct backing access in their methods") {
    gdpp::DiagnosticBag diagnostics;
    const auto module = lower_source("var active: bool = true: set = set_active, get = is_active\n"
                                     "func set_active(value: bool) -> void:\n"
                                     "    active = value\n"
                                     "func is_active() -> bool:\n"
                                     "    return active\n"
                                     "func disable() -> void:\n"
                                     "    active = false\n",
                                     diagnostics);

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(module.fields.front().setter->method, std::string{"set_active"});
    REQUIRE_EQ(module.fields.front().getter->method, std::string{"is_active"});
    REQUIRE_EQ(module.functions.at(0).body.front().condition->resolution,
               gdpp::ir::ResolutionKind::none);
    REQUIRE_EQ(module.functions.at(1).body.front().expression->resolution,
               gdpp::ir::ResolutionKind::none);
    REQUIRE_EQ(module.functions.at(2).body.front().condition->resolution,
               gdpp::ir::ResolutionKind::script_property);
}
