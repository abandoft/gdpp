#include "support/test.hpp"

#include "gdpp/semantic/flow.hpp"

TEST_CASE("flow states apply, invalidate and join type refinements") {
    const gdpp::Type node{gdpp::TypeKind::object, "Node"};
    const gdpp::Type node_2d{gdpp::TypeKind::object, "Node2D"};

    gdpp::FlowTypeState left;
    left.refine(1, node);
    left.refine(2, node_2d);
    left.mark_non_null(1);
    left.mark_non_null(2);

    gdpp::FlowTypeState right;
    right.refine(1, node);
    right.refine(2, node);
    right.mark_non_null(1);

    gdpp::FlowTypeState third;
    third.refine(1, node);
    third.refine(3, node_2d);
    third.mark_non_null(1);

    const std::vector<const gdpp::FlowTypeState*> predecessors{&left, &right, &third};
    const auto joined = gdpp::FlowTypeState::join_fallthrough(predecessors);
    REQUIRE(joined.find(1) != nullptr);
    REQUIRE_EQ(*joined.find(1), node);
    REQUIRE(joined.find(2) == nullptr);
    REQUIRE(joined.is_non_null(1));
    REQUIRE(!joined.is_non_null(2));

    left.invalidate(1);
    REQUIRE(left.find(1) == nullptr);
    left.clear();
    REQUIRE(left.refinements().empty());
    REQUIRE(left.non_null_symbols().empty());
}

TEST_CASE("conditional refinements compose sequential and alternative paths") {
    const gdpp::Type integer{gdpp::TypeKind::integer, "int"};
    const gdpp::Type node{gdpp::TypeKind::object, "Node"};
    const gdpp::Type node_2d{gdpp::TypeKind::object, "Node2D"};

    const gdpp::FlowRefinements first{{{1, node}, {2, integer}}, {1, 2}};
    const gdpp::FlowRefinements second{{{1, node_2d}, {3, integer}}, {1, 3}};
    const auto sequence = gdpp::sequence_refinements(first, second);
    REQUIRE_EQ(sequence.types.at(1), node_2d);
    REQUIRE_EQ(sequence.types.at(2), integer);
    REQUIRE_EQ(sequence.types.at(3), integer);
    REQUIRE_EQ(sequence.non_null.size(), std::size_t{3});

    const gdpp::FlowRefinements alternative{{{1, node}, {2, node_2d}}, {1, 3}};
    const auto common = gdpp::common_refinements(first, alternative);
    REQUIRE_EQ(common.types.size(), std::size_t{1});
    REQUIRE_EQ(common.types.at(1), node);
    REQUIRE_EQ(common.non_null.size(), std::size_t{1});
    REQUIRE(common.non_null.find(1) != common.non_null.end());
}
