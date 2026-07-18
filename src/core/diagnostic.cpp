#include "gdpp/core/diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace gdpp {

void DiagnosticBag::report(Diagnostic diagnostic) {
    if (diagnostic.severity == DiagnosticSeverity::error)
        has_error_ = true;
    if (diagnostics_.size() < max_diagnostics_) {
        diagnostics_.push_back(std::move(diagnostic));
        return;
    }
    if (!limit_reported_) {
        diagnostics_.push_back({diagnostic.severity, "GDS0001",
                                "diagnostic limit reached; further diagnostics are suppressed",
                                diagnostic.span});
        limit_reported_ = true;
    } else if (diagnostic.severity == DiagnosticSeverity::error && !diagnostics_.empty()) {
        diagnostics_.back().severity = DiagnosticSeverity::error;
    }
}

void DiagnosticBag::error(std::string code, std::string message, SourceSpan span) {
    report({DiagnosticSeverity::error, std::move(code), std::move(message), span});
}

void DiagnosticBag::warning(std::string code, std::string message, SourceSpan span) {
    report({DiagnosticSeverity::warning, std::move(code), std::move(message), span});
}

bool DiagnosticBag::has_errors() const noexcept { return has_error_; }

std::string format_diagnostic(const Diagnostic& diagnostic, const SourceFile& source,
                              bool use_color) {
    const char* severity = "error";
    const char* color = "\033[31m";
    if (diagnostic.severity == DiagnosticSeverity::warning) {
        severity = "warning";
        color = "\033[33m";
    } else if (diagnostic.severity == DiagnosticSeverity::note) {
        severity = "note";
        color = "\033[36m";
    }

    std::ostringstream output;
    output << source.path() << ':' << diagnostic.span.begin.line << ':'
           << diagnostic.span.begin.column << ": ";
    if (use_color) {
        output << color;
    }
    output << severity;
    if (use_color) {
        output << "\033[0m";
    }
    output << '[' << diagnostic.code << "]: " << diagnostic.message << '\n';

    const auto source_line = source.line(diagnostic.span.begin.line);
    if (!source_line.empty()) {
        output << "  " << source_line << '\n' << "  ";
        const auto column = std::max<std::size_t>(diagnostic.span.begin.column, 1);
        output << std::string(column - 1, ' ') << '^';
        const auto width = diagnostic.span.end.line == diagnostic.span.begin.line
                               ? diagnostic.span.end.column - diagnostic.span.begin.column
                               : 1;
        output << std::string(width > 1 ? width - 1 : 0, '~') << '\n';
    }
    return output.str();
}

} // namespace gdpp
