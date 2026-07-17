#include "gdpp/diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace gdpp {

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)), line_offsets_{0} {
    for (std::size_t index = 0; index < text_.size(); ++index) {
        if (text_[index] == '\n') {
            line_offsets_.push_back(index + 1);
        }
    }
}

std::string_view SourceFile::slice(const SourceSpan& span) const noexcept {
    const auto begin = std::min(span.begin.offset, text_.size());
    const auto end = std::min(std::max(span.end.offset, begin), text_.size());
    return std::string_view{text_}.substr(begin, end - begin);
}

std::string_view SourceFile::line(std::size_t one_based_line) const noexcept {
    if (one_based_line == 0 || one_based_line > line_offsets_.size()) {
        return {};
    }
    const auto begin = line_offsets_[one_based_line - 1];
    auto end = text_.find('\n', begin);
    if (end == std::string::npos) {
        end = text_.size();
    }
    if (end > begin && text_[end - 1] == '\r') {
        --end;
    }
    return std::string_view{text_}.substr(begin, end - begin);
}

void DiagnosticBag::report(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

void DiagnosticBag::error(std::string code, std::string message, SourceSpan span) {
    report({DiagnosticSeverity::error, std::move(code), std::move(message), span});
}

void DiagnosticBag::warning(std::string code, std::string message, SourceSpan span) {
    report({DiagnosticSeverity::warning, std::move(code), std::move(message), span});
}

bool DiagnosticBag::has_errors() const noexcept {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::error;
    });
}

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
