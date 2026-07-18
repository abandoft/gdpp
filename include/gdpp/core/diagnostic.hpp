#pragma once

#include "gdpp/core/source.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace gdpp {

enum class DiagnosticSeverity { note, warning, error };

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string code;
    std::string message;
    SourceSpan span{};
};

class DiagnosticBag final {
  public:
    explicit DiagnosticBag(std::size_t max_diagnostics = 256U)
        : max_diagnostics_(max_diagnostics) {}

    void report(Diagnostic diagnostic);
    void error(std::string code, std::string message, SourceSpan span);
    void warning(std::string code, std::string message, SourceSpan span);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }
    [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept { return diagnostics_; }

  private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t max_diagnostics_{256U};
    bool has_error_{false};
    bool limit_reported_{false};
};

[[nodiscard]] std::string format_diagnostic(const Diagnostic& diagnostic, const SourceFile& source,
                                            bool use_color = false);

} // namespace gdpp
