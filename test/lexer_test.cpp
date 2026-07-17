#include "support/test.hpp"

#include "gdpp/diagnostic.hpp"
#include "gdpp/lexer.hpp"
#include "gdpp/source.hpp"
#include "gdpp/token.hpp"

#include <algorithm>

TEST_CASE("lexer emits structural indentation tokens") {
    const gdpp::SourceFile source{"memory.gd", "func ready() -> void:\n"
                                               "    var score: int = 41\n"
                                               "    score += 1\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.front().kind, gdpp::TokenKind::kw_func);
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::indent;
            }) != tokens.end());
    REQUIRE_EQ(tokens[tokens.size() - 2].kind, gdpp::TokenKind::dedent);
    REQUIRE_EQ(tokens.back().kind, gdpp::TokenKind::end_of_file);
}

TEST_CASE("lexer decodes strings and ignores comments") {
    const gdpp::SourceFile source{"memory.gd", "var text = \"hello\\nworld\" # comment\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();
    const auto string_token =
        std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
            return token.kind == gdpp::TokenKind::string;
        });

    REQUIRE(!diagnostics.has_errors());
    REQUIRE(string_token != tokens.end());
    REQUIRE_EQ(string_token->lexeme, std::string{"hello\nworld"});
}

TEST_CASE("lexer distinguishes an empty string from an omitted token lexeme") {
    const gdpp::SourceFile source{"memory.gd", "var text := \"\"\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();
    const auto string_token =
        std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
            return token.kind == gdpp::TokenKind::string;
        });

    REQUIRE(!diagnostics.has_errors());
    REQUIRE(string_token != tokens.end());
    REQUIRE(string_token->lexeme.empty());
}

TEST_CASE("lexer recognizes GDScript annotations") {
    const gdpp::SourceFile source{"exported.gd", "@export_range(0, 100) var health: int = 100\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.front().kind, gdpp::TokenKind::at_sign);
    REQUIRE_EQ(tokens.at(1).lexeme, std::string{"export_range"});
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::kw_var;
            }) != tokens.end());
}

TEST_CASE("lexer recognizes enum declarations") {
    const gdpp::SourceFile source{"state.gd", "enum State { IDLE, RUN = 4 }\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.front().kind, gdpp::TokenKind::kw_enum);
    REQUIRE_EQ(tokens.at(1).lexeme, std::string{"State"});
}

TEST_CASE("lexer recognizes assert as a statement keyword") {
    const gdpp::SourceFile source{"assert.gd", "assert(true, \"ready\")\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.front().kind, gdpp::TokenKind::kw_assert);
}

TEST_CASE("lexer recognizes modern literals node shorthand and symbolic operators") {
    const gdpp::SourceFile source{"modern.gd", "var mask := 0xff_00 | 0b1010\n"
                                               "var name := &\"player\"\n"
                                               "var path := ^\"Root/Child\"\n"
                                               "var node := $Root/Child\n"
                                               "var unique := %Hud\n"
                                               "var value := 2 ** 3\n"
                                               "value **= 2\n"
                                               "value %= 3\n"
                                               "var ok := true && !false || false\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    const auto has = [&](gdpp::TokenKind kind) {
        return std::find_if(tokens.begin(), tokens.end(), [kind](const gdpp::Token& token) {
                   return token.kind == kind;
               }) != tokens.end();
    };
    REQUIRE(has(gdpp::TokenKind::string_name));
    REQUIRE(has(gdpp::TokenKind::node_path));
    REQUIRE(has(gdpp::TokenKind::node_reference));
    REQUIRE(has(gdpp::TokenKind::power));
    REQUIRE(has(gdpp::TokenKind::power_equal));
    REQUIRE(has(gdpp::TokenKind::percent_equal));
}

TEST_CASE("node shorthand stops before member access") {
    const gdpp::SourceFile source{"nodes.gd", "$Animation.play(&\"take\")\n%Hud.show()\n"};
    gdpp::DiagnosticBag diagnostics;
    gdpp::Lexer lexer{source, diagnostics};
    const auto tokens = lexer.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.at(0).kind, gdpp::TokenKind::node_reference);
    REQUIRE_EQ(tokens.at(0).lexeme, std::string{"$Animation"});
    REQUIRE_EQ(tokens.at(1).kind, gdpp::TokenKind::dot);
    REQUIRE_EQ(tokens.at(7).kind, gdpp::TokenKind::node_reference);
    REQUIRE_EQ(tokens.at(7).lexeme, std::string{"%Hud"});
}

TEST_CASE("lexer distinguishes compact modulo from unique-node shorthand") {
    const gdpp::SourceFile source{"commercial_nodes.gd",
                                  "var index := randi()%len($\"../../..\".enemy_kind)\n"
                                  "var volume := %\"全局音量条\".value\n"
                                  "var compact := value%divisor\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::percent; }),
               std::ptrdiff_t{2});
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::node_reference && token.lexeme == "$../../..";
            }) != tokens.end());
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::node_reference &&
                       token.lexeme == "%全局音量条";
            }) != tokens.end());
}

TEST_CASE("lexer accepts compact floats and compound unique-node paths") {
    const gdpp::SourceFile source{"commercial_literals.gd",
                                  "var leading := .2\n"
                                  "var trailing := 4.\n"
                                  "var exponent := .5e+2\n"
                                  "var unique := $%Hud\n"
                                  "var nested := $%BomberMesh/%ParticleSparks\n"
                                  "var relative := $../Camera\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(std::count_if(tokens.begin(), tokens.end(),
                             [](const gdpp::Token& token) {
                                 return token.kind == gdpp::TokenKind::floating;
                             }),
               std::ptrdiff_t{3});
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::node_reference && token.lexeme == "$%Hud";
            }) != tokens.end());
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::node_reference &&
                       token.lexeme == "$%BomberMesh/%ParticleSparks";
            }) != tokens.end());
    REQUIRE(std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::node_reference &&
                       token.lexeme == "$../Camera";
            }) != tokens.end());
}

TEST_CASE("lexer preserves lambda blocks inside continued call arguments") {
    const gdpp::SourceFile source{"lambda.gd", "func ready(signal_value: Signal) -> void:\n"
                                               "    signal_value.connect(\n"
                                               "        func(value: int) -> void:\n"
                                               "            print(value)\n"
                                               "            )\n"
                                               "    signal_value.connect(\n"
                                               "        func() -> void:\n"
                                               "            print(1))\n"
                                               "    pass\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::kw_func; }),
               std::ptrdiff_t{3});
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::indent; }),
               std::ptrdiff_t{3});
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::dedent; }),
               std::ptrdiff_t{3});
}

TEST_CASE("lexer handles explicit line continuation and rejects malformed separators") {
    const gdpp::SourceFile continued{"continued.gd", R"(var value := 1 + \
        2
)"};
    gdpp::DiagnosticBag continued_diagnostics;
    const auto tokens = gdpp::Lexer{continued, continued_diagnostics}.scan();

    REQUIRE(!continued_diagnostics.has_errors());
    REQUIRE(std::count_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
                return token.kind == gdpp::TokenKind::newline;
            }) == 1);

    const gdpp::SourceFile malformed{"malformed.gd", "var value := 1__2\n"};
    gdpp::DiagnosticBag malformed_diagnostics;
    (void)gdpp::Lexer{malformed, malformed_diagnostics}.scan();
    REQUIRE(malformed_diagnostics.has_errors());
}

TEST_CASE("lexer ignores UTF-8 BOM and preserves multiline strings") {
    const gdpp::SourceFile source{"utf8.gd", "\xef\xbb\xbf"
                                             "extends Node\n"
                                             "var message = \"first\nsecond\"\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(tokens.front().kind, gdpp::TokenKind::kw_extends);
    const auto string_token =
        std::find_if(tokens.begin(), tokens.end(), [](const gdpp::Token& token) {
            return token.kind == gdpp::TokenKind::string;
        });
    REQUIRE(string_token != tokens.end());
    REQUIRE_EQ(string_token->lexeme, std::string{"first\nsecond"});
}

TEST_CASE("lexer preserves nested lambda layout and suppresses collection layout") {
    const gdpp::SourceFile source{"nested_lambdas.gd", "func run() -> void:\n"
                                                       "    visit(func(item):\n"
                                                       "        call(func(value):\n"
                                                       "            print(value)\n"
                                                       "        , {\n"
                                                       "            \"item\": item,\n"
                                                       "            \"value\": 1\n"
                                                       "        })\n"
                                                       "    , \"done\")\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::indent; }),
               std::ptrdiff_t{3});
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::dedent; }),
               std::ptrdiff_t{3});
}

TEST_CASE("lexer carries explicit continuation across comment-only lines") {
    const gdpp::SourceFile source{"continued_comments.gd", "func run() -> void:\n"
                                                           "    var enabled = \\\n"
                                                           "        # first condition\n"
                                                           "        true || \\\n"
                                                           "        # second condition\n"
                                                           "        false\n"};
    gdpp::DiagnosticBag diagnostics;
    const auto tokens = gdpp::Lexer{source, diagnostics}.scan();

    REQUIRE(!diagnostics.has_errors());
    REQUIRE_EQ(std::count_if(
                   tokens.begin(), tokens.end(),
                   [](const gdpp::Token& token) { return token.kind == gdpp::TokenKind::newline; }),
               std::ptrdiff_t{2});
}
