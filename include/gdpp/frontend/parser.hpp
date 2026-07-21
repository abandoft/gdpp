#pragma once

#include "gdpp/core/diagnostic.hpp"
#include "gdpp/frontend/ast.hpp"
#include "gdpp/frontend/limits.hpp"
#include "gdpp/frontend/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gdpp {

class Parser final {
  public:
    Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics,
           FrontendLimits limits = {});
    [[nodiscard]] ast::Script parse_script();

  private:
    struct ParsedParameters {
        std::vector<ast::Parameter> positional;
        std::optional<ast::Parameter> rest;
    };

    class DepthGuard final {
      public:
        DepthGuard(Parser& parser, SourceSpan span);
        ~DepthGuard();
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
        [[nodiscard]] explicit operator bool() const noexcept { return active_; }

      private:
        Parser& parser_;
        bool active_{false};
    };

    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    [[nodiscard]] bool check_soft_identifier() const noexcept;
    [[nodiscard]] bool check_attribute_name() const noexcept;
    [[nodiscard]] bool is_match_statement_ahead() const noexcept;
    bool match(TokenKind kind) noexcept;
    bool match_inferred_assignment() noexcept;
    const Token& advance() noexcept;
    const Token& consume(TokenKind kind, const char* message);
    const Token& consume_soft_identifier(const char* message);
    const Token& consume_attribute_name(const char* message);
    void skip_newlines() noexcept;
    void synchronize() noexcept;

    [[nodiscard]] std::string parse_type_name(const char* message);
    [[nodiscard]] std::optional<std::string> parse_type_annotation();
    [[nodiscard]] ast::Annotation parse_annotation();
    void apply_warning_directive(const ast::Annotation& annotation);
    [[nodiscard]] ParsedParameters parse_parameters(std::string_view owner,
                                                    bool allow_defaults = true,
                                                    bool allow_rest = true);
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
    void apply_active_warning_ignores(ast::Statement& statement) const;
    [[nodiscard]] std::vector<ast::Statement> parse_block();
    [[nodiscard]] std::vector<ast::Statement> parse_suite();
    [[nodiscard]] ast::Statement parse_statement();
    [[nodiscard]] ast::Statement parse_assert_statement();
    [[nodiscard]] ast::Statement parse_if_statement();
    [[nodiscard]] ast::Statement parse_match_statement();
    [[nodiscard]] ast::MatchBranch parse_match_branch();
    [[nodiscard]] std::unique_ptr<ast::MatchPattern> parse_match_pattern();
    [[nodiscard]] ast::Statement parse_while_statement();
    [[nodiscard]] ast::Statement parse_for_statement();
    [[nodiscard]] ast::Statement parse_variable_statement(bool is_constant);

    [[nodiscard]] ast::ExpressionPtr parse_expression(int minimum_precedence = 0);
    [[nodiscard]] ast::ExpressionPtr parse_prefix();
    [[nodiscard]] ast::ExpressionPtr parse_lambda(SourceSpan begin);
    [[nodiscard]] ast::ExpressionPtr parse_postfix(ast::ExpressionPtr expression);
    [[nodiscard]] static int precedence(TokenKind kind) noexcept;
    [[nodiscard]] static bool is_binary_operator(TokenKind kind) noexcept;

    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    FrontendLimits limits_;
    std::size_t position_{0};
    std::size_t recursion_depth_{0};
    bool recursion_limit_reported_{false};
    std::unordered_map<std::string, SourceSpan> ignored_warning_ranges_;
};

} // namespace gdpp
