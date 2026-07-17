#pragma once

#include "gdpp/core/source.hpp"

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
    void report(Diagnostic diagnostic);
    void error(std::string code, std::string message, SourceSpan span);
    void warning(std::string code, std::string message, SourceSpan span);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }
    [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept { return diagnostics_; }

  private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::string format_diagnostic(const Diagnostic& diagnostic, const SourceFile& source,
                                            bool use_color = false);

} // namespace gdpp
