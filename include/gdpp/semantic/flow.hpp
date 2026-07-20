#pragma once

#include "gdpp/semantic/type.hpp"

#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gdpp {

using FlowSymbolId = std::uint64_t;
using TypeRefinements = std::unordered_map<FlowSymbolId, Type>;

struct FlowRefinements {
    TypeRefinements types;
    std::unordered_set<FlowSymbolId> non_null;
};

struct ConditionalRefinements {
    FlowRefinements when_true;
    FlowRefinements when_false;
};

// A flow state contains only facts that are stronger than a symbol's declared storage type.
// Symbols keep their stable identity across AST references, while branch states are cheap values
// that can be copied, joined and restored without mutating lexical scopes.
class FlowTypeState final {
  public:
    [[nodiscard]] const Type* find(FlowSymbolId symbol) const noexcept;
    [[nodiscard]] bool is_non_null(FlowSymbolId symbol) const noexcept;
    void refine(FlowSymbolId symbol, Type type);
    void mark_non_null(FlowSymbolId symbol);
    void invalidate(FlowSymbolId symbol) noexcept;
    void apply(const FlowRefinements& refinements);
    void clear() noexcept;

    [[nodiscard]] const TypeRefinements& refinements() const noexcept { return refinements_; }
    [[nodiscard]] const std::unordered_set<FlowSymbolId>& non_null_symbols() const noexcept {
        return non_null_symbols_;
    }

    [[nodiscard]] static FlowTypeState
    join_fallthrough(std::initializer_list<const FlowTypeState*> predecessors);
    [[nodiscard]] static FlowTypeState
    join_fallthrough(const std::vector<const FlowTypeState*>& predecessors);

  private:
    TypeRefinements refinements_;
    std::unordered_set<FlowSymbolId> non_null_symbols_;
};

[[nodiscard]] FlowRefinements sequence_refinements(const FlowRefinements& first,
                                                   const FlowRefinements& second);
[[nodiscard]] FlowRefinements common_refinements(const FlowRefinements& first,
                                                 const FlowRefinements& second);

} // namespace gdpp
