#include "gdpp/semantic/flow.hpp"

#include <utility>

namespace gdpp {

const Type* FlowTypeState::find(const FlowSymbolId symbol) const noexcept {
    const auto found = refinements_.find(symbol);
    return found == refinements_.end() ? nullptr : &found->second;
}

void FlowTypeState::refine(const FlowSymbolId symbol, Type type) {
    if (symbol != 0)
        refinements_.insert_or_assign(symbol, std::move(type));
}

void FlowTypeState::invalidate(const FlowSymbolId symbol) noexcept {
    refinements_.erase(symbol);
}

void FlowTypeState::apply(const TypeRefinements& refinements) {
    for (const auto& [symbol, type] : refinements)
        refine(symbol, type);
}

void FlowTypeState::clear() noexcept {
    refinements_.clear();
}

FlowTypeState
FlowTypeState::join_fallthrough(const std::initializer_list<const FlowTypeState*> predecessors) {
    FlowTypeState result;
    const FlowTypeState* first = nullptr;
    for (const auto* predecessor : predecessors) {
        if (predecessor) {
            first = predecessor;
            break;
        }
    }
    if (!first)
        return result;

    result.refinements_ = first->refinements_;
    for (const auto* predecessor : predecessors) {
        if (!predecessor || predecessor == first)
            continue;
        for (auto fact = result.refinements_.begin(); fact != result.refinements_.end();) {
            const auto candidate = predecessor->refinements_.find(fact->first);
            if (candidate == predecessor->refinements_.end() || candidate->second != fact->second) {
                fact = result.refinements_.erase(fact);
            } else {
                ++fact;
            }
        }
    }
    return result;
}

TypeRefinements sequence_refinements(const TypeRefinements& first,
                                     const TypeRefinements& second) {
    auto result = first;
    for (const auto& [symbol, type] : second)
        result.insert_or_assign(symbol, type);
    return result;
}

TypeRefinements common_refinements(const TypeRefinements& first,
                                   const TypeRefinements& second) {
    TypeRefinements result;
    const auto* smaller = &first;
    const auto* larger = &second;
    if (smaller->size() > larger->size())
        std::swap(smaller, larger);
    for (const auto& [symbol, type] : *smaller) {
        const auto found = larger->find(symbol);
        if (found != larger->end() && found->second == type)
            result.emplace(symbol, type);
    }
    return result;
}

} // namespace gdpp
