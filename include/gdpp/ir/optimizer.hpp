#pragma once

#include "gdpp/ir/hir.hpp"

#include <cstddef>

namespace gdpp {

struct OptimizationStats {
    std::size_t constants_folded{0};
    std::size_t statements_removed{0};
};

class IrOptimizer final {
  public:
    [[nodiscard]] OptimizationStats optimize(ir::Module& module) const;

  private:
    void optimize_expression(ir::Expression& expression, OptimizationStats& stats) const;
    void optimize_match_pattern(ir::MatchPattern& pattern, OptimizationStats& stats) const;
    void optimize_statements(std::vector<ir::Statement>& statements,
                             OptimizationStats& stats) const;
    void optimize_class(ir::Class& declaration, OptimizationStats& stats) const;
};

} // namespace gdpp
