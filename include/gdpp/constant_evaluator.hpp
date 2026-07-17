#pragma once

#include "gdpp/ast.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace gdpp {

[[nodiscard]] std::optional<std::int64_t>
evaluate_integer_constant(const ast::Expression& expression,
                          const std::unordered_map<std::string, std::int64_t>& previous = {});

} // namespace gdpp
