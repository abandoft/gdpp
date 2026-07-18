#include "support/test.hpp"

#include "gdpp/ir/mir.hpp"

#include <algorithm>

namespace {

gdpp::ir::ExpressionPtr literal(std::string value = "true") {
    auto result = std::make_unique<gdpp::ir::Expression>();
    result->kind = gdpp::ir::ExpressionKind::literal;
    result->literal_kind =
        value == "true" ? gdpp::ir::LiteralKind::boolean : gdpp::ir::LiteralKind::integer;
    result->value = std::move(value);
    return result;
}

gdpp::ir::Statement marker(gdpp::ir::StatementKind kind) {
    gdpp::ir::Statement result;
    result.kind = kind;
    return result;
}

} // namespace

TEST_CASE("MIR builds explicit branches loops returns and suspension edges") {
    gdpp::ir::Module hir;
    hir.class_name = "ControlFlow";
    gdpp::ir::Function function;
    function.name = "run";

    gdpp::ir::Statement conditional;
    conditional.kind = gdpp::ir::StatementKind::if_statement;
    conditional.condition = literal();
    conditional.body.push_back(marker(gdpp::ir::StatementKind::return_statement));
    conditional.else_body.push_back(marker(gdpp::ir::StatementKind::pass_statement));
    function.body.push_back(std::move(conditional));

    gdpp::ir::Statement loop;
    loop.kind = gdpp::ir::StatementKind::while_statement;
    loop.condition = literal();
    loop.body.push_back(marker(gdpp::ir::StatementKind::continue_statement));
    function.body.push_back(std::move(loop));

    gdpp::ir::Statement await;
    await.kind = gdpp::ir::StatementKind::await_statement;
    await.expression = literal();
    function.body.push_back(std::move(await));
    hir.functions.push_back(std::move(function));

    const auto mir = gdpp::MirLowerer{}.lower(hir);
    gdpp::DiagnosticBag diagnostics;
    REQUIRE(gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(mir.functions.size(), std::size_t{1});
    REQUIRE(mir.functions.front().suspends);
    REQUIRE(std::any_of(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                        [](const gdpp::mir::BasicBlock& block) {
                            return block.terminator.kind == gdpp::mir::TerminatorKind::branch;
                        }));
    REQUIRE(std::any_of(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                        [](const gdpp::mir::BasicBlock& block) {
                            return block.terminator.kind == gdpp::mir::TerminatorKind::suspend;
                        }));
}

TEST_CASE("MIR suspends for a typed coroutine call with a scalar logical result") {
    gdpp::ir::Module hir;
    gdpp::ir::Function function;
    function.name = "consume";
    function.is_coroutine = true;

    gdpp::ir::Statement await;
    await.kind = gdpp::ir::StatementKind::await_variable;
    await.name = "value";
    await.declared_type = {gdpp::TypeKind::integer, "int"};
    await.expression = std::make_unique<gdpp::ir::Expression>();
    await.expression->kind = gdpp::ir::ExpressionKind::call;
    await.expression->type = {gdpp::TypeKind::integer, "int"};
    await.expression->coroutine_call = true;
    function.body.push_back(std::move(await));
    hir.functions.push_back(std::move(function));

    const auto mir = gdpp::MirLowerer{}.lower(hir);
    gdpp::DiagnosticBag diagnostics;
    REQUIRE(gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE(mir.functions.front().suspends);
    REQUIRE(std::any_of(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                        [](const gdpp::mir::BasicBlock& block) {
                            return block.terminator.kind == gdpp::mir::TerminatorKind::suspend;
                        }));
}

TEST_CASE("MIR covers accessors internal methods and lambdas") {
    gdpp::ir::Module hir;
    hir.class_name = "Owners";
    gdpp::ir::Field field;
    field.name = "value";
    field.getter = gdpp::ir::PropertyAccessor{};
    field.getter->body.push_back(marker(gdpp::ir::StatementKind::return_statement));
    hir.fields.push_back(std::move(field));

    gdpp::ir::Function function;
    function.name = "make";
    gdpp::ir::Statement expression_statement;
    expression_statement.kind = gdpp::ir::StatementKind::expression;
    expression_statement.expression = std::make_unique<gdpp::ir::Expression>();
    expression_statement.expression->kind = gdpp::ir::ExpressionKind::lambda;
    expression_statement.expression->lambda = std::make_unique<gdpp::ir::LambdaExpression>();
    expression_statement.expression->lambda->body.push_back(
        marker(gdpp::ir::StatementKind::return_statement));
    function.body.push_back(std::move(expression_statement));
    hir.functions.push_back(std::move(function));

    gdpp::ir::Class inner;
    inner.name = "Inner";
    gdpp::ir::Function inner_method;
    inner_method.name = "call";
    inner_method.body.push_back(marker(gdpp::ir::StatementKind::pass_statement));
    inner.functions.push_back(std::move(inner_method));
    hir.classes.push_back(std::move(inner));

    const auto mir = gdpp::MirLowerer{}.lower(hir);
    gdpp::DiagnosticBag diagnostics;
    REQUIRE(gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE_EQ(mir.functions.size(), std::size_t{4});
    REQUIRE(std::any_of(mir.functions.begin(), mir.functions.end(), [](const auto& item) {
        return item.role == gdpp::mir::FunctionRole::lambda;
    }));
}

TEST_CASE("MIR places awaited match guards between pattern tests and branch bodies") {
    gdpp::ir::Module hir;
    gdpp::ir::Function function;
    function.name = "run";

    gdpp::ir::Statement match;
    match.kind = gdpp::ir::StatementKind::match_statement;
    match.condition = literal("1");
    gdpp::ir::Statement branch;
    branch.kind = gdpp::ir::StatementKind::match_branch;
    gdpp::ir::MatchPattern wildcard;
    wildcard.kind = gdpp::ir::MatchPatternKind::wildcard;
    branch.patterns.push_back(std::move(wildcard));
    branch.expression = literal();
    gdpp::ir::Statement await;
    await.kind = gdpp::ir::StatementKind::await_variable;
    await.name = "guard";
    await.expression = literal();
    await.expression->type = {gdpp::TypeKind::builtin, "Signal"};
    branch.guard_prefix.push_back(std::move(await));
    branch.body.push_back(marker(gdpp::ir::StatementKind::pass_statement));
    match.body.push_back(std::move(branch));
    function.body.push_back(std::move(match));
    hir.functions.push_back(std::move(function));

    const auto mir = gdpp::MirLowerer{}.lower(hir);
    gdpp::DiagnosticBag diagnostics;
    REQUIRE(gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE(mir.functions.front().suspends);
    REQUIRE(std::any_of(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                        [](const gdpp::mir::BasicBlock& block) {
                            return block.terminator.kind == gdpp::mir::TerminatorKind::suspend;
                        }));
    REQUIRE(std::count_if(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                          [](const gdpp::mir::BasicBlock& block) {
                              return block.terminator.kind == gdpp::mir::TerminatorKind::branch;
                          }) >= 2);
}

TEST_CASE("MIR keeps awaited assert messages on the failure-only edge") {
    gdpp::ir::Module hir;
    gdpp::ir::Function function;
    function.name = "validate";

    gdpp::ir::Statement assertion;
    assertion.kind = gdpp::ir::StatementKind::assert_statement;
    assertion.condition = literal();
    assertion.expression = literal();
    gdpp::ir::Statement condition_await;
    condition_await.kind = gdpp::ir::StatementKind::await_variable;
    condition_await.name = "condition";
    condition_await.expression = literal();
    condition_await.expression->type = {gdpp::TypeKind::builtin, "Signal"};
    assertion.assert_condition_prefix.push_back(std::move(condition_await));
    gdpp::ir::Statement message_await;
    message_await.kind = gdpp::ir::StatementKind::await_variable;
    message_await.name = "message";
    message_await.expression = literal();
    message_await.expression->type = {gdpp::TypeKind::builtin, "Signal"};
    assertion.assert_message_prefix.push_back(std::move(message_await));
    function.body.push_back(std::move(assertion));
    function.body.push_back(marker(gdpp::ir::StatementKind::pass_statement));
    hir.functions.push_back(std::move(function));

    const auto mir = gdpp::MirLowerer{}.lower(hir);
    gdpp::DiagnosticBag diagnostics;
    REQUIRE(gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE(mir.functions.front().suspends);
    REQUIRE_EQ(static_cast<std::size_t>(std::count_if(
                   mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                   [](const gdpp::mir::BasicBlock& block) {
                       return block.terminator.kind == gdpp::mir::TerminatorKind::suspend;
                   })),
               std::size_t{2});
    REQUIRE(std::any_of(mir.functions.front().blocks.begin(), mir.functions.front().blocks.end(),
                        [](const gdpp::mir::BasicBlock& block) {
                            return block.terminator.kind == gdpp::mir::TerminatorKind::stop;
                        }));
}

TEST_CASE("MIR verifier rejects corrupt edge and predecessor metadata") {
    gdpp::ir::Module hir;
    gdpp::ir::Function function;
    function.name = "broken";
    function.body.push_back(marker(gdpp::ir::StatementKind::pass_statement));
    hir.functions.push_back(std::move(function));
    auto mir = gdpp::MirLowerer{}.lower(hir);
    mir.functions.front().blocks.front().terminator.targets.push_back(999);

    gdpp::DiagnosticBag diagnostics;
    REQUIRE(!gdpp::MirVerifier{diagnostics}.verify(mir));
    REQUIRE(diagnostics.has_errors());
}
