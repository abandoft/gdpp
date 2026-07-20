#pragma once

#include "gdpp/semantic/type.hpp"

#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <vector>

namespace gdpp {

using FlowSymbolId = std::uint64_t;
using TypeRefinements = std::unordered_map<FlowSymbolId, Type>;

struct ConditionalRefinements {
    TypeRefinements when_true;
    TypeRefinements when_false;
};

// A flow state contains only facts that are stronger than a symbol's declared storage type.
// Symbols keep their stable identity across AST references, while branch states are cheap values
// that can be copied, joined and restored without mutating lexical scopes.
class FlowTypeState final {
  public:
    [[nodiscard]] const Type* find(FlowSymbolId symbol) const noexcept;
    void refine(FlowSymbolId symbol, Type type);
    void invalidate(FlowSymbolId symbol) noexcept;
    void apply(const TypeRefinements& refinements);
    void clear() noexcept;

    [[nodiscard]] const TypeRefinements& refinements() const noexcept { return refinements_; }

    [[nodiscard]] static FlowTypeState
    join_fallthrough(std::initializer_list<const FlowTypeState*> predecessors);
    [[nodiscard]] static FlowTypeState
    join_fallthrough(const std::vector<const FlowTypeState*>& predecessors);

  private:
    TypeRefinements refinements_;
};

[[nodiscard]] TypeRefinements sequence_refinements(const TypeRefinements& first,
                                                   const TypeRefinements& second);
[[nodiscard]] TypeRefinements common_refinements(const TypeRefinements& first,
                                                 const TypeRefinements& second);

} // namespace gdpp
