#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gdpp {

// These predicates use the same Unicode XID tables as the pinned latest stable Godot tokenizer.
// They intentionally do not normalize identifiers: distinct source codepoint sequences remain
// distinct symbols, matching Godot's lexer contract.
[[nodiscard]] bool is_unicode_identifier_start(std::uint32_t codepoint) noexcept;
[[nodiscard]] bool is_unicode_identifier_continue(std::uint32_t codepoint) noexcept;

// Produces the Unicode 17.0 UTS #39 confusable skeleton used by Godot 4.7's ICU-backed
// TextServer. Invalid UTF-8 fails closed with nullopt; no host ICU or locale is consulted.
[[nodiscard]] std::optional<std::string>
unicode_confusable_skeleton(std::string_view utf8_identifier);

} // namespace gdpp
