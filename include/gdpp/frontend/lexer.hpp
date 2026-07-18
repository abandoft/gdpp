#pragma once

#include "gdpp/core/diagnostic.hpp"
#include "gdpp/core/source.hpp"
#include "gdpp/frontend/limits.hpp"
#include "gdpp/frontend/token.hpp"

#include <optional>
#include <vector>

namespace gdpp {

class Lexer final {
  public:
    Lexer(const SourceFile& source, DiagnosticBag& diagnostics, FrontendLimits limits = {});
    [[nodiscard]] std::vector<Token> scan();

  private:
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char peek(std::size_t distance = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    [[nodiscard]] SourceLocation location() const noexcept;
    void validate_utf8();
    void report_limit(const char* resource, SourceSpan span);
    [[nodiscard]] bool literal_within_limit(SourceLocation begin);
    void emit_eof();
    void emit(TokenKind kind, SourceLocation begin,
              std::optional<std::string> lexeme = std::nullopt);
    void scan_indentation();
    void scan_number(SourceLocation begin);
    void scan_identifier(SourceLocation begin);
    void scan_string(SourceLocation begin, char quote, TokenKind kind = TokenKind::string,
                     bool raw = false, bool triple_quoted = false);
    void scan_node_reference(SourceLocation begin, char prefix);
    void finish_lambda_block(SourceLocation delimiter);
    [[nodiscard]] bool lambda_layout_active() const noexcept;

    struct LambdaBlockContext {
        std::size_t indentation_base{0};
        std::size_t grouping_depth{0};
    };

    const SourceFile& source_;
    DiagnosticBag& diagnostics_;
    FrontendLimits limits_;
    std::vector<Token> tokens_;
    std::vector<std::size_t> indent_stack_{0};
    char indentation_character_{'\0'};
    std::size_t offset_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    bool at_line_start_{true};
    bool halted_{false};
    bool explicit_line_continuation_{false};
    std::size_t grouping_depth_{0};
    bool lambda_signature_in_group_{false};
    std::size_t lambda_signature_grouping_depth_{0};
    std::vector<LambdaBlockContext> lambda_blocks_;
};

} // namespace gdpp
