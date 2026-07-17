#pragma once

#include "gdpp/ast.hpp"
#include "gdpp/diagnostic.hpp"
#include "gdpp/ir.hpp"
#include "gdpp/semantic.hpp"

namespace gdpp {

class IrLowerer final {
  public:
    explicit IrLowerer(const SemanticModel& semantic) : semantic_(semantic) {}
    [[nodiscard]] ir::Module lower(const ast::Script& script) const;

  private:
    [[nodiscard]] ir::ExpressionPtr lower_expression(const ast::Expression& expression) const;
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
    [[nodiscard]] bool verify_statement(const ir::Statement& statement);
    [[nodiscard]] bool verify_class(const ir::Class& declaration);

    DiagnosticBag& diagnostics_;
};

} // namespace gdpp
