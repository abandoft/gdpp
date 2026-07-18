#pragma once

#include "gdpp/ir/mir.hpp"

#include <cstddef>

namespace gdpp {

struct MirOptimizationStats {
    std::size_t branches_simplified{0};
    std::size_t blocks_removed{0};
    std::size_t instructions_removed{0};
};

// Performs semantics-preserving CFG transforms after MIR verification. Every transform must
// rebuild dense block IDs and predecessor metadata so the result remains a valid codegen input.
class MirOptimizer final {
  public:
    [[nodiscard]] MirOptimizationStats optimize(mir::Module& module) const;
};

} // namespace gdpp
