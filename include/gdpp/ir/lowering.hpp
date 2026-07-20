#pragma once

#include "gdpp/core/diagnostic.hpp"
#include "gdpp/frontend/ast.hpp"
#include "gdpp/ir/hir.hpp"
#include "gdpp/semantic/analyzer.hpp"

namespace gdpp {

class IrLowerer final {
  public:
    explicit IrLowerer(const SemanticModel& semantic) : semantic_(semantic) {}
    [[nodiscard]] ir::Module lower(const ast::Script& script) const;

  private:
    [[nodiscard]] ir::ExpressionPtr lower_expression(const ast::Expression& expression) const;
    [[nodiscard]] ir::MatchPattern lower_match_pattern(const ast::MatchPattern& pattern) const;
    [[nodiscard]] ir::Statement lower_statement(const ast::Statement& statement) const;
    [[nodiscard]] ir::Parameter lower_parameter(const ast::Parameter& parameter) const;
    [[nodiscard]] ir::Class lower_class(const ast::ClassDeclaration& declaration) const;

    const SemanticModel& semantic_;
};

class IrVerifier final {
  public:
    explicit IrVerifier(DiagnosticBag& diagnostics) : diagnostics_(diagnostics) {}
    [[nodiscard]] bool verify(const ir::Module& module);

  private:
    [[nodiscard]] bool verify_expression(const ir::Expression& expression);
    [[nodiscard]] bool verify_parameter(const ir::Parameter& parameter);
    [[nodiscard]] bool verify_match_pattern(const ir::MatchPattern& pattern);
    [[nodiscard]] bool verify_statement(const ir::Statement& statement);
    [[nodiscard]] bool verify_class(const ir::Class& declaration);

    DiagnosticBag& diagnostics_;
};

} // namespace gdpp
