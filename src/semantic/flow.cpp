#include "gdpp/semantic/flow.hpp"

#include <utility>

namespace gdpp {

const Type* FlowTypeState::find(const FlowSymbolId symbol) const noexcept {
    const auto found = refinements_.find(symbol);
    return found == refinements_.end() ? nullptr : &found->second;
}

bool FlowTypeState::is_non_null(const FlowSymbolId symbol) const noexcept {
    return non_null_symbols_.find(symbol) != non_null_symbols_.end();
}

void FlowTypeState::refine(const FlowSymbolId symbol, Type type) {
    if (symbol != 0)
        refinements_.insert_or_assign(symbol, std::move(type));
}

void FlowTypeState::mark_non_null(const FlowSymbolId symbol) {
    if (symbol != 0)
        non_null_symbols_.insert(symbol);
}

void FlowTypeState::invalidate(const FlowSymbolId symbol) noexcept {
    refinements_.erase(symbol);
    non_null_symbols_.erase(symbol);
}

void FlowTypeState::apply(const FlowRefinements& refinements) {
    for (const auto& [symbol, type] : refinements.types)
        refine(symbol, type);
    for (const auto symbol : refinements.non_null)
        mark_non_null(symbol);
}

void FlowTypeState::clear() noexcept {
    refinements_.clear();
    non_null_symbols_.clear();
}

FlowTypeState
FlowTypeState::join_fallthrough(const std::initializer_list<const FlowTypeState*> predecessors) {
    return join_fallthrough(std::vector<const FlowTypeState*>{predecessors});
}

FlowTypeState
FlowTypeState::join_fallthrough(const std::vector<const FlowTypeState*>& predecessors) {
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
    result.non_null_symbols_ = first->non_null_symbols_;
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
        for (auto symbol = result.non_null_symbols_.begin();
             symbol != result.non_null_symbols_.end();) {
            if (!predecessor->is_non_null(*symbol)) {
                symbol = result.non_null_symbols_.erase(symbol);
            } else {
                ++symbol;
            }
        }
    }
    return result;
}

FlowRefinements sequence_refinements(const FlowRefinements& first, const FlowRefinements& second) {
    auto result = first;
    for (const auto& [symbol, type] : second.types)
        result.types.insert_or_assign(symbol, type);
    result.non_null.insert(second.non_null.begin(), second.non_null.end());
    return result;
}

FlowRefinements common_refinements(const FlowRefinements& first, const FlowRefinements& second) {
    FlowRefinements result;
    const auto* smaller = &first.types;
    const auto* larger = &second.types;
    if (smaller->size() > larger->size())
        std::swap(smaller, larger);
    for (const auto& [symbol, type] : *smaller) {
        const auto found = larger->find(symbol);
        if (found != larger->end() && found->second == type)
            result.types.emplace(symbol, type);
    }
    const auto* smaller_non_null = &first.non_null;
    const auto* larger_non_null = &second.non_null;
    if (smaller_non_null->size() > larger_non_null->size())
        std::swap(smaller_non_null, larger_non_null);
    for (const auto symbol : *smaller_non_null) {
        if (larger_non_null->find(symbol) != larger_non_null->end())
            result.non_null.insert(symbol);
    }
    return result;
}

} // namespace gdpp
