#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gdpp {

struct SourceLocation {
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};
};

struct SourceSpan {
    SourceLocation begin{};
    SourceLocation end{};
};

class SourceFile final {
  public:
    SourceFile(std::string path, std::string text);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::string_view slice(const SourceSpan& span) const noexcept;
    [[nodiscard]] std::string_view line(std::size_t one_based_line) const noexcept;

  private:
    std::string path_;
    std::string text_;
    std::vector<std::size_t> line_offsets_;
};

} // namespace gdpp
