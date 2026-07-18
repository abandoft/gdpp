#include "gdpp/frontend/unicode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdpp {
namespace {

struct UnicodeRange {
    std::uint32_t first;
    std::uint32_t last;
};

struct UnicodeMapping {
    std::uint32_t codepoint;
    std::uint32_t offset;
    std::uint16_t length;
};

struct UnicodeCombiningClass {
    std::uint32_t codepoint;
    std::uint8_t value;
};

#include "unicode_identifier_ranges.inc"

template <std::size_t Size>
bool contains(const std::array<UnicodeRange, Size>& ranges, std::uint32_t codepoint) noexcept {
    const auto match = std::lower_bound(
        ranges.begin(), ranges.end(), codepoint,
        [](const UnicodeRange& range, std::uint32_t value) { return range.last < value; });
    return match != ranges.end() && codepoint >= match->first;
}

template <std::size_t Size>
const UnicodeMapping* find_mapping(const std::array<UnicodeMapping, Size>& mappings,
                                   const std::uint32_t codepoint) noexcept {
    const auto match =
        std::lower_bound(mappings.begin(), mappings.end(), codepoint,
                         [](const UnicodeMapping& mapping, const std::uint32_t value) {
                             return mapping.codepoint < value;
                         });
    return match != mappings.end() && match->codepoint == codepoint ? &*match : nullptr;
}

std::uint8_t canonical_combining_class(const std::uint32_t codepoint) noexcept {
    const auto match = std::lower_bound(
        canonical_combining_classes.begin(), canonical_combining_classes.end(), codepoint,
        [](const UnicodeCombiningClass& value, const std::uint32_t target) {
            return value.codepoint < target;
        });
    return match != canonical_combining_classes.end() && match->codepoint == codepoint
               ? match->value
               : 0U;
}

void append_canonical_decomposition(const std::uint32_t codepoint,
                                    std::vector<std::uint32_t>& output) {
    constexpr std::uint32_t hangul_s_base = 0xac00U;
    constexpr std::uint32_t hangul_l_base = 0x1100U;
    constexpr std::uint32_t hangul_v_base = 0x1161U;
    constexpr std::uint32_t hangul_t_base = 0x11a7U;
    constexpr std::uint32_t hangul_l_count = 19U;
    constexpr std::uint32_t hangul_v_count = 21U;
    constexpr std::uint32_t hangul_t_count = 28U;
    constexpr std::uint32_t hangul_n_count = hangul_v_count * hangul_t_count;
    constexpr std::uint32_t hangul_s_count = hangul_l_count * hangul_n_count;
    if (codepoint >= hangul_s_base && codepoint < hangul_s_base + hangul_s_count) {
        const auto index = codepoint - hangul_s_base;
        output.push_back(hangul_l_base + index / hangul_n_count);
        output.push_back(hangul_v_base + (index % hangul_n_count) / hangul_t_count);
        const auto trailing = index % hangul_t_count;
        if (trailing != 0U)
            output.push_back(hangul_t_base + trailing);
        return;
    }
    const auto* mapping = find_mapping(canonical_decomposition_mappings, codepoint);
    if (!mapping) {
        output.push_back(codepoint);
        return;
    }
    for (std::size_t index = 0; index < mapping->length; ++index) {
        append_canonical_decomposition(canonical_decomposition_values[mapping->offset + index],
                                       output);
    }
}

void canonical_order(std::vector<std::uint32_t>& codepoints) {
    for (std::size_t index = 1; index < codepoints.size(); ++index) {
        const auto combining_class = canonical_combining_class(codepoints[index]);
        if (combining_class == 0U)
            continue;
        auto position = index;
        while (position > 0U) {
            const auto previous_class = canonical_combining_class(codepoints[position - 1U]);
            if (previous_class == 0U || previous_class <= combining_class)
                break;
            std::swap(codepoints[position], codepoints[position - 1U]);
            --position;
        }
    }
}

std::vector<std::uint32_t> canonical_decompose(const std::vector<std::uint32_t>& input) {
    std::vector<std::uint32_t> output;
    output.reserve(input.size());
    for (const auto codepoint : input)
        append_canonical_decomposition(codepoint, output);
    canonical_order(output);
    return output;
}

std::optional<std::vector<std::uint32_t>> decode_utf8(const std::string_view text) {
    std::vector<std::uint32_t> output;
    output.reserve(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7fU) {
            length = 1;
            codepoint = first;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            length = 2;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            length = 3;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            length = 4;
            codepoint = first & 0x07U;
        } else {
            return std::nullopt;
        }
        if (offset + length > text.size())
            return std::nullopt;
        for (std::size_t index = 1; index < length; ++index) {
            const auto continuation = static_cast<unsigned char>(text[offset + index]);
            if ((continuation & 0xc0U) != 0x80U)
                return std::nullopt;
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        const bool overlong = (length == 2U && codepoint < 0x80U) ||
                              (length == 3U && codepoint < 0x800U) ||
                              (length == 4U && codepoint < 0x10000U);
        if (overlong || codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return std::nullopt;
        }
        output.push_back(codepoint);
        offset += length;
    }
    return output;
}

bool append_utf8(std::string& output, const std::uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

} // namespace

bool is_unicode_identifier_start(std::uint32_t codepoint) noexcept {
    return contains(xid_start_ranges, codepoint);
}

bool is_unicode_identifier_continue(std::uint32_t codepoint) noexcept {
    return contains(xid_continue_ranges, codepoint);
}

std::optional<std::string> unicode_confusable_skeleton(const std::string_view utf8_identifier) {
    const auto decoded = decode_utf8(utf8_identifier);
    if (!decoded)
        return std::nullopt;
    const auto normalized = canonical_decompose(*decoded);
    std::vector<std::uint32_t> mapped;
    mapped.reserve(normalized.size());
    for (const auto codepoint : normalized) {
        if (contains(default_ignorable_ranges, codepoint))
            continue;
        if (const auto* mapping = find_mapping(confusable_mappings, codepoint)) {
            for (std::size_t index = 0; index < mapping->length; ++index)
                mapped.push_back(confusable_values[mapping->offset + index]);
        } else {
            mapped.push_back(codepoint);
        }
    }
    const auto skeleton_codepoints = canonical_decompose(mapped);
    std::string skeleton;
    skeleton.reserve(utf8_identifier.size());
    for (const auto codepoint : skeleton_codepoints) {
        if (!append_utf8(skeleton, codepoint))
            return std::nullopt;
    }
    return skeleton;
}

} // namespace gdpp
