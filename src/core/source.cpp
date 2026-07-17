#include "gdpp/core/source.hpp"

#include <algorithm>
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

} // namespace gdpp
