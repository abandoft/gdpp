#pragma once

#include "gdpp/core/diagnostic.hpp"
#include "gdpp/frontend/ast.hpp"
#include "gdpp/frontend/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gdpp {

class Parser final {
  public:
    Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics);
    [[nodiscard]] ast::Script parse_script();

  private:
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    bool match(TokenKind kind) noexcept;
    const Token& advance() noexcept;
    const Token& consume(TokenKind kind, const char* message);
    void skip_newlines() noexcept;
    void synchronize() noexcept;

    [[nodiscard]] std::string parse_type_name(const char* message);
    [[nodiscard]] std::optional<std::string> parse_type_annotation();
    [[nodiscard]] ast::Annotation parse_annotation();
    [[nodiscard]] std::vector<ast::Parameter> parse_parameters();
    [[nodiscard]] ast::VariableDeclaration parse_variable(bool is_constant,
                                                          std::vector<ast::Annotation> annotations);
    void parse_property_accessors(ast::VariableDeclaration& declaration);
    void parse_bound_property_accessors(ast::VariableDeclaration& declaration, bool indented);
    [[nodiscard]] ast::SignalDeclaration parse_signal(std::vector<ast::Annotation> annotations);
    [[nodiscard]] ast::EnumDeclaration parse_enum(std::vector<ast::Annotation> annotations);
    [[nodiscard]] ast::FunctionDeclaration parse_function(bool is_static,
                                                          std::vector<ast::Annotation> annotations);
    [[nodiscard]] ast::ClassDeclaration parse_class(std::vector<ast::Annotation> annotations);
    void parse_class_member(ast::ClassDeclaration& declaration,
                            std::vector<ast::Annotation>& annotations);
    [[nodiscard]] std::vector<ast::Statement> parse_block();
    [[nodiscard]] std::vector<ast::Statement> parse_suite();
    [[nodiscard]] ast::Statement parse_statement();
    [[nodiscard]] ast::Statement parse_assert_statement();
    [[nodiscard]] ast::Statement parse_if_statement();
    [[nodiscard]] ast::Statement parse_match_statement();
    [[nodiscard]] ast::MatchBranch parse_match_branch();
    [[nodiscard]] ast::Statement parse_while_statement();
    [[nodiscard]] ast::Statement parse_for_statement();
    [[nodiscard]] ast::Statement parse_variable_statement();

    [[nodiscard]] ast::ExpressionPtr parse_expression(int minimum_precedence = 0);
    [[nodiscard]] ast::ExpressionPtr parse_prefix();
    [[nodiscard]] ast::ExpressionPtr parse_lambda(SourceSpan begin);
    [[nodiscard]] ast::ExpressionPtr parse_postfix(ast::ExpressionPtr expression);
    [[nodiscard]] static int precedence(TokenKind kind) noexcept;
    [[nodiscard]] static bool is_binary_operator(TokenKind kind) noexcept;

    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    std::size_t position_{0};
};

} // namespace gdpp
