#include "support/test.hpp"

#include "gdpp/compiler/compiler.hpp"
#include "gdpp/core/diagnostic.hpp"
#include "gdpp/core/source.hpp"
#include "gdpp/frontend/lexer.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

bool has_code(const gdpp::DiagnosticBag& diagnostics, const std::string& code) {
    return std::any_of(diagnostics.items().begin(), diagnostics.items().end(),
                       [&](const gdpp::Diagnostic& diagnostic) { return diagnostic.code == code; });
}

bool has_code(const gdpp::CompileResult& result, const std::string& code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const gdpp::Diagnostic& diagnostic) { return diagnostic.code == code; });
}

gdpp::FrontendLimits compact_limits() {
    gdpp::FrontendLimits limits;
    limits.max_source_bytes = 512;
    limits.max_line_bytes = 128;
    limits.max_tokens = 128;
    limits.max_literal_bytes = 64;
    limits.max_indentation_depth = 16;
    limits.max_grouping_depth = 32;
    limits.max_parser_depth = 32;
    limits.max_diagnostics = 16;
    return limits;
}

} // namespace

TEST_CASE("diagnostic bag bounds hostile error fanout without hiding failure") {
    gdpp::DiagnosticBag diagnostics{2};
    diagnostics.warning("TEST0001", "first", {});
    diagnostics.warning("TEST0002", "second", {});
    diagnostics.warning("TEST0003", "suppressed", {});
    diagnostics.error("TEST0004", "also suppressed", {});

    REQUIRE(diagnostics.has_errors());
    REQUIRE_EQ(diagnostics.items().size(), std::size_t{3});
    REQUIRE_EQ(diagnostics.items().back().code, std::string{"GDS0001"});
    REQUIRE_EQ(diagnostics.items().back().severity, gdpp::DiagnosticSeverity::error);
}

TEST_CASE("lexer enforces every configurable input resource boundary") {
    const auto expect_limit = [](std::string text, gdpp::FrontendLimits limits) {
        const gdpp::SourceFile source{"limited.gd", std::move(text)};
        gdpp::DiagnosticBag diagnostics{limits.max_diagnostics};
        const auto tokens = gdpp::Lexer{source, diagnostics, limits}.scan();
        REQUIRE(diagnostics.has_errors());
        REQUIRE(has_code(diagnostics, "GDS1010"));
        REQUIRE(!tokens.empty());
        REQUIRE_EQ(tokens.back().kind, gdpp::TokenKind::end_of_file);
        REQUIRE(tokens.size() <= limits.max_tokens + 1U);
    };

    auto limits = compact_limits();
    limits.max_source_bytes = 8;
    expect_limit("123456789", limits);

    limits = compact_limits();
    limits.max_line_bytes = 8;
    expect_limit("123456789\n", limits);

    limits = compact_limits();
    limits.max_tokens = 3;
    expect_limit("var value := 1\n", limits);

    limits = compact_limits();
    limits.max_literal_bytes = 8;
    expect_limit("var value := \"a very long literal\"\n", limits);

    limits = compact_limits();
    limits.max_indentation_depth = 2;
    expect_limit("if true:\n    if true:\n        if true:\n            pass\n", limits);

    limits = compact_limits();
    limits.max_grouping_depth = 2;
    expect_limit("var value := (((1)))\n", limits);
}

TEST_CASE("compiler stops parser recursion transactionally at the configured depth") {
    gdpp::CompileOptions options;
    options.frontend_limits = compact_limits();
    options.frontend_limits.max_source_bytes = 4096;
    options.frontend_limits.max_line_bytes = 4096;
    options.frontend_limits.max_grouping_depth = 128;
    options.frontend_limits.max_parser_depth = 8;

    std::string source{"func nested() -> int:\n    return "};
    source.append(32, '(');
    source += '1';
    source.append(32, ')');
    source += '\n';
    const auto result = gdpp::Compiler{}.compile("deep.gd", std::move(source), options);

    REQUIRE(!result.success);
    REQUIRE(has_code(result, "GDS2024"));
    REQUIRE(result.unit.header.empty());
    REQUIRE(result.unit.source.empty());
}

TEST_CASE("frontend limits preserve valid audited scripts below their budgets") {
    gdpp::CompileOptions options;
    options.frontend_limits = compact_limits();
    const auto result = gdpp::Compiler{}.compile(
        "bounded.gd", "extends Node\nfunc answer() -> int:\n    return (40 + 2)\n", options);

    REQUIRE(result.success);
    REQUIRE(!result.unit.header.empty());
    REQUIRE(!result.unit.source.empty());
}

TEST_CASE("deterministic hostile frontend corpus is bounded and transactional") {
    gdpp::CompileOptions options;
    options.frontend_limits = compact_limits();
    std::uint32_t state = 0x6d2b79f5U;
    auto next = [&]() {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    };

    for (std::size_t case_index = 0; case_index < 512; ++case_index) {
        std::string source;
        const auto size = static_cast<std::size_t>(next() % 384U);
        source.reserve(size);
        for (std::size_t index = 0; index < size; ++index)
            source.push_back(static_cast<char>(next() & 0xffU));

        const auto result = gdpp::Compiler{}.compile(
            "hostile_" + std::to_string(case_index) + ".gd", std::move(source), options);
        REQUIRE(result.metrics.token_count <= options.frontend_limits.max_tokens + 1U);
        REQUIRE(result.diagnostics.size() <= options.frontend_limits.max_diagnostics + 1U);
        if (!result.success) {
            REQUIRE(result.unit.header.empty());
            REQUIRE(result.unit.source.empty());
        }
    }
}
