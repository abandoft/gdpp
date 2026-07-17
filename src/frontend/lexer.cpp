#include "gdpp/frontend/lexer.hpp"

#include <cctype>
#include <string_view>
#include <unordered_map>

namespace gdpp {
namespace {

bool is_identifier_start(char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalpha(byte) != 0 || value == '_' || byte >= 0x80U;
}

bool is_identifier_continue(char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_' || byte >= 0x80U;
}

bool can_end_expression(TokenKind kind) {
    switch (kind) {
    case TokenKind::identifier:
    case TokenKind::integer:
    case TokenKind::floating:
    case TokenKind::string:
    case TokenKind::string_name:
    case TokenKind::node_path:
    case TokenKind::node_reference:
    case TokenKind::kw_true:
    case TokenKind::kw_false:
    case TokenKind::kw_null:
    case TokenKind::kw_self:
    case TokenKind::right_paren:
    case TokenKind::right_bracket:
    case TokenKind::right_brace:
        return true;
    default:
        return false;
    }
}

const std::unordered_map<std::string_view, TokenKind> keywords{
    {"extends", TokenKind::kw_extends}, {"class_name", TokenKind::kw_class_name},
    {"class", TokenKind::kw_class},     {"func", TokenKind::kw_func},
    {"var", TokenKind::kw_var},         {"const", TokenKind::kw_const},
    {"signal", TokenKind::kw_signal},   {"enum", TokenKind::kw_enum},
    {"static", TokenKind::kw_static},   {"return", TokenKind::kw_return},
    {"assert", TokenKind::kw_assert},   {"pass", TokenKind::kw_pass},
    {"if", TokenKind::kw_if},           {"match", TokenKind::kw_match},
    {"when", TokenKind::kw_when},       {"elif", TokenKind::kw_elif},
    {"else", TokenKind::kw_else},       {"while", TokenKind::kw_while},
    {"for", TokenKind::kw_for},         {"in", TokenKind::kw_in},
    {"break", TokenKind::kw_break},     {"continue", TokenKind::kw_continue},
    {"true", TokenKind::kw_true},       {"false", TokenKind::kw_false},
    {"null", TokenKind::kw_null},       {"self", TokenKind::kw_self},
    {"and", TokenKind::kw_and},         {"or", TokenKind::kw_or},
    {"not", TokenKind::kw_not},         {"as", TokenKind::kw_as},
    {"is", TokenKind::kw_is},           {"await", TokenKind::kw_await},
};

} // namespace

Lexer::Lexer(const SourceFile& source, DiagnosticBag& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

bool Lexer::at_end() const noexcept { return offset_ >= source_.text().size(); }

char Lexer::peek(std::size_t distance) const noexcept {
    const auto target = offset_ + distance;
    return target < source_.text().size() ? source_.text()[target] : '\0';
}

char Lexer::advance() noexcept {
    const char value = peek();
    if (at_end()) {
        return '\0';
    }
    ++offset_;
    if (value == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return value;
}

bool Lexer::match(char expected) noexcept {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

SourceLocation Lexer::location() const noexcept { return {offset_, line_, column_}; }

void Lexer::emit(TokenKind kind, SourceLocation begin, std::optional<std::string> lexeme) {
    const auto end = location();
    if (!lexeme.has_value()) {
        lexeme = std::string{source_.text().substr(begin.offset, end.offset - begin.offset)};
    }
    tokens_.push_back({kind, std::move(*lexeme), {begin, end}});
}

void Lexer::scan_indentation() {
    const auto begin = location();
    std::size_t width = 0;
    while (peek() == ' ' || peek() == '\t') {
        if (advance() == '\t') {
            width = ((width / 4) + 1) * 4;
        } else {
            ++width;
        }
    }

    if (peek() == '\n' || peek() == '\r' || peek() == '#' || at_end()) {
        return;
    }

    at_line_start_ = false;
    if (width > indent_stack_.back()) {
        indent_stack_.push_back(width);
        emit(TokenKind::indent, begin, std::to_string(width));
        return;
    }
    while (width < indent_stack_.back()) {
        indent_stack_.pop_back();
        emit(TokenKind::dedent, begin, std::to_string(width));
    }
    if (width != indent_stack_.back()) {
        diagnostics_.error("GDS1001", "indentation does not match an outer block",
                           {begin, location()});
    }
}

void Lexer::scan_number(SourceLocation begin) {
    const auto scan_digits = [&](const auto& is_digit, const char* message) {
        bool saw_digit = false;
        bool previous_separator = false;
        while (is_digit(peek()) || peek() == '_') {
            if (peek() == '_') {
                if (!saw_digit || previous_separator) {
                    diagnostics_.error("GDS1006", message, {begin, location()});
                }
                previous_separator = true;
            } else {
                saw_digit = true;
                previous_separator = false;
            }
            advance();
        }
        if (!saw_digit || previous_separator) {
            diagnostics_.error("GDS1006", message, {begin, location()});
        }
        return saw_digit;
    };

    const bool leading_decimal_point = source_.text()[begin.offset] == '.';

    if (!leading_decimal_point && source_.text()[begin.offset] == '0' &&
        (peek() == 'x' || peek() == 'X')) {
        advance();
        scan_digits(
            [](char value) { return std::isxdigit(static_cast<unsigned char>(value)) != 0; },
            "invalid hexadecimal integer literal");
        emit(TokenKind::integer, begin);
        return;
    }
    if (!leading_decimal_point && source_.text()[begin.offset] == '0' &&
        (peek() == 'b' || peek() == 'B')) {
        advance();
        scan_digits([](char value) { return value == '0' || value == '1'; },
                    "invalid binary integer literal");
        emit(TokenKind::integer, begin);
        return;
    }

    auto kind = leading_decimal_point ? TokenKind::floating : TokenKind::integer;
    if (leading_decimal_point) {
        scan_digits([](char value) { return std::isdigit(static_cast<unsigned char>(value)) != 0; },
                    "invalid fractional part");
    } else {
        bool previous_separator = false;
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0 || peek() == '_') {
            if (peek() == '_') {
                if (previous_separator)
                    diagnostics_.error("GDS1006", "invalid numeric separator", {begin, location()});
                previous_separator = true;
            } else {
                previous_separator = false;
            }
            advance();
        }
        if (previous_separator)
            diagnostics_.error("GDS1006", "numeric literal cannot end with '_'",
                               {begin, location()});

        // GDScript accepts both `1.0` and the compact trailing-dot form `1.`.
        if (peek() == '.') {
            kind = TokenKind::floating;
            advance();
            if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                scan_digits(
                    [](char value) { return std::isdigit(static_cast<unsigned char>(value)) != 0; },
                    "invalid fractional part");
            }
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        kind = TokenKind::floating;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        scan_digits([](char value) { return std::isdigit(static_cast<unsigned char>(value)) != 0; },
                    "invalid exponent");
    }
    emit(kind, begin);
}

void Lexer::scan_identifier(SourceLocation begin) {
    while (is_identifier_continue(peek())) {
        advance();
    }
    const auto text = std::string_view{source_.text()}.substr(begin.offset, offset_ - begin.offset);
    const auto keyword = keywords.find(text);
    const auto kind = keyword == keywords.end() ? TokenKind::identifier : keyword->second;
    emit(kind, begin);
    if (kind == TokenKind::kw_func && grouping_depth_ > 0) {
        lambda_signature_in_group_ = true;
        lambda_signature_grouping_depth_ = grouping_depth_;
    }
}

void Lexer::scan_string(SourceLocation begin, char quote, TokenKind kind) {
    std::string value;
    while (!at_end() && peek() != quote) {
        char current = advance();
        if (current == '\\') {
            if (at_end()) {
                break;
            }
            const char escaped = advance();
            switch (escaped) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '\'':
                value.push_back('\'');
                break;
            case '"':
                value.push_back('"');
                break;
            default:
                diagnostics_.warning("GDS1003", "unknown escape sequence", {begin, location()});
                value.push_back(escaped);
                break;
            }
        } else {
            value.push_back(current);
        }
    }
    if (at_end()) {
        diagnostics_.error("GDS1002", "unterminated string literal", {begin, location()});
        return;
    }
    advance();
    emit(kind, begin, std::move(value));
}

void Lexer::scan_node_reference(SourceLocation begin, char prefix) {
    std::string value(1, prefix);
    bool has_path = false;
    if (peek() == '\'' || peek() == '"') {
        const char quote = advance();
        while (!at_end() && peek() != quote && peek() != '\n') {
            value.push_back(advance());
            has_path = true;
        }
        if (!match(quote)) {
            diagnostics_.error("GDS1002", "unterminated node path", {begin, location()});
        }
    } else {
        // `$%UniqueNode` and `$%Parent/%Child` are standard Godot node shorthands.
        // Keep the percent sign in the NodePath instead of tokenizing it as modulo.
        if (prefix == '$' && peek() == '%')
            value.push_back(advance());
        while (!at_end()) {
            const char current = peek();
            if (prefix == '$' && current == '.' && peek(1) == '.' &&
                (value.size() == 1 || value.back() == '/')) {
                value.push_back(advance());
                value.push_back(advance());
                has_path = true;
                continue;
            }
            if (is_identifier_continue(current) || current == '/' || current == '-' ||
                current == '@' || current == '%') {
                value.push_back(advance());
                has_path = true;
                continue;
            }
            break;
        }
    }
    if (!has_path) {
        diagnostics_.error("GDS1005", "expected a node path after node shorthand",
                           {begin, location()});
    }
    emit(TokenKind::node_reference, begin, std::move(value));
}

void Lexer::finish_lambda_block(const SourceLocation delimiter) {
    if (lambda_blocks_.empty())
        return;
    const auto context = lambda_blocks_.back();
    if (tokens_.empty() || tokens_.back().kind != TokenKind::newline)
        emit(TokenKind::newline, delimiter, "\\n");
    while (indent_stack_.back() > context.indentation_base) {
        indent_stack_.pop_back();
        emit(TokenKind::dedent, delimiter, std::to_string(context.indentation_base));
    }
    lambda_blocks_.pop_back();
}

bool Lexer::lambda_layout_active() const noexcept {
    return !lambda_blocks_.empty() && grouping_depth_ == lambda_blocks_.back().grouping_depth;
}

std::vector<Token> Lexer::scan() {
    if (source_.text().size() >= 3 && static_cast<unsigned char>(source_.text()[0]) == 0xefU &&
        static_cast<unsigned char>(source_.text()[1]) == 0xbbU &&
        static_cast<unsigned char>(source_.text()[2]) == 0xbfU) {
        // Preserve byte offsets while treating an optional UTF-8 BOM as transport metadata.
        offset_ = 3;
    }
    while (!at_end()) {
        if (at_line_start_ && (grouping_depth_ == 0 || lambda_layout_active())) {
            scan_indentation();
            while (!lambda_blocks_.empty() && !at_line_start_ &&
                   indent_stack_.back() <= lambda_blocks_.back().indentation_base)
                lambda_blocks_.pop_back();
            if (at_end()) {
                break;
            }
        }

        const auto begin = location();
        const char value = advance();
        if (explicit_line_continuation_ && value != ' ' && value != '\t' && value != '\r' &&
            value != '\n' && value != '#')
            explicit_line_continuation_ = false;
        switch (value) {
        case ' ':
        case '\t':
        case '\r':
            break;
        case '#':
            while (!at_end() && peek() != '\n') {
                advance();
            }
            break;
        case '\n':
            if (!explicit_line_continuation_ && (grouping_depth_ == 0 || lambda_layout_active())) {
                emit(TokenKind::newline, begin, "\\n");
                at_line_start_ = true;
            }
            break;
        case '(':
            ++grouping_depth_;
            emit(TokenKind::left_paren, begin);
            break;
        case ')':
            if (lambda_layout_active())
                finish_lambda_block(begin);
            if (grouping_depth_ > 0)
                --grouping_depth_;
            emit(TokenKind::right_paren, begin);
            break;
        case '[':
            ++grouping_depth_;
            emit(TokenKind::left_bracket, begin);
            break;
        case ']':
            if (lambda_layout_active())
                finish_lambda_block(begin);
            if (grouping_depth_ > 0)
                --grouping_depth_;
            emit(TokenKind::right_bracket, begin);
            break;
        case '{':
            ++grouping_depth_;
            emit(TokenKind::left_brace, begin);
            break;
        case '}':
            if (lambda_layout_active())
                finish_lambda_block(begin);
            if (grouping_depth_ > 0)
                --grouping_depth_;
            emit(TokenKind::right_brace, begin);
            break;
        case '@':
            emit(TokenKind::at_sign, begin);
            break;
        case ',':
            if (lambda_layout_active())
                finish_lambda_block(begin);
            emit(TokenKind::comma, begin);
            break;
        case '.':
            if (std::isdigit(static_cast<unsigned char>(peek())) != 0)
                scan_number(begin);
            else
                emit(TokenKind::dot, begin);
            break;
        case ':':
            if (match('=')) {
                emit(TokenKind::colon_equal, begin);
            } else {
                emit(TokenKind::colon, begin);
                std::size_t lookahead = 0;
                while (peek(lookahead) == ' ' || peek(lookahead) == '\t' ||
                       peek(lookahead) == '\r') {
                    ++lookahead;
                }
                if (lambda_signature_in_group_ &&
                    grouping_depth_ == lambda_signature_grouping_depth_) {
                    lambda_signature_in_group_ = false;
                    if (peek(lookahead) == '\n') {
                        lambda_blocks_.push_back({indent_stack_.back(), grouping_depth_});
                    }
                }
            }
            break;
        case ';':
            emit(TokenKind::semicolon, begin);
            break;
        case '+':
            emit(match('=') ? TokenKind::plus_equal : TokenKind::plus, begin);
            break;
        case '-':
            if (match('>'))
                emit(TokenKind::arrow, begin);
            else
                emit(match('=') ? TokenKind::minus_equal : TokenKind::minus, begin);
            break;
        case '*':
            if (match('*'))
                emit(match('=') ? TokenKind::power_equal : TokenKind::power, begin);
            else
                emit(match('=') ? TokenKind::star_equal : TokenKind::star, begin);
            break;
        case '/':
            emit(match('=') ? TokenKind::slash_equal : TokenKind::slash, begin);
            break;
        case '%':
            if (match('=')) {
                emit(TokenKind::percent_equal, begin);
            } else if ((is_identifier_start(peek()) || peek() == '\'' || peek() == '"') &&
                       (tokens_.empty() || !can_end_expression(tokens_.back().kind))) {
                scan_node_reference(begin, '%');
            } else {
                emit(TokenKind::percent, begin);
            }
            break;
        case '=':
            emit(match('=') ? TokenKind::equal_equal : TokenKind::equal, begin);
            break;
        case '!':
            if (match('='))
                emit(TokenKind::bang_equal, begin);
            else
                emit(TokenKind::kw_not, begin, "not");
            break;
        case '<':
            if (match('='))
                emit(TokenKind::less_equal, begin);
            else if (match('<'))
                emit(match('=') ? TokenKind::shift_left_equal : TokenKind::shift_left, begin);
            else
                emit(TokenKind::less, begin);
            break;
        case '>':
            if (match('='))
                emit(TokenKind::greater_equal, begin);
            else if (match('>'))
                emit(match('=') ? TokenKind::shift_right_equal : TokenKind::shift_right, begin);
            else
                emit(TokenKind::greater, begin);
            break;
        case '&':
            if (peek() == '\'' || peek() == '"') {
                const char quote = advance();
                scan_string(begin, quote, TokenKind::string_name);
            } else if (match('&')) {
                emit(TokenKind::kw_and, begin, "and");
            } else {
                emit(match('=') ? TokenKind::ampersand_equal : TokenKind::ampersand, begin);
            }
            break;
        case '|':
            if (match('|'))
                emit(TokenKind::kw_or, begin, "or");
            else
                emit(match('=') ? TokenKind::pipe_equal : TokenKind::pipe, begin);
            break;
        case '^':
            if (peek() == '\'' || peek() == '"') {
                const char quote = advance();
                scan_string(begin, quote, TokenKind::node_path);
            } else {
                emit(match('=') ? TokenKind::caret_equal : TokenKind::caret, begin);
            }
            break;
        case '~':
            emit(TokenKind::tilde, begin);
            break;
        case '\'':
        case '"':
            scan_string(begin, value);
            break;
        case '$':
            scan_node_reference(begin, '$');
            break;
        case '\\':
            if (peek() == '\r' && peek(1) == '\n')
                advance();
            if (match('\n')) {
                at_line_start_ = false;
                explicit_line_continuation_ = true;
            } else {
                diagnostics_.error("GDS1004", "unexpected backslash", {begin, location()});
            }
            break;
        default:
            if (std::isdigit(static_cast<unsigned char>(value)) != 0) {
                scan_number(begin);
            } else if (is_identifier_start(value)) {
                scan_identifier(begin);
            } else {
                diagnostics_.error("GDS1005", "unexpected character", {begin, location()});
            }
            break;
        }
    }

    const auto eof = location();
    if (!tokens_.empty() && tokens_.back().kind != TokenKind::newline && grouping_depth_ == 0) {
        emit(TokenKind::newline, eof, "\\n");
    }
    while (indent_stack_.size() > 1) {
        indent_stack_.pop_back();
        emit(TokenKind::dedent, eof, "0");
    }
    emit(TokenKind::end_of_file, eof, "");
    return tokens_;
}

} // namespace gdpp
