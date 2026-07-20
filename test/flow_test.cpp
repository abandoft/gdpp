#include "support/test.hpp"

#include "gdpp/semantic/flow.hpp"

TEST_CASE("flow states apply, invalidate and join type refinements") {
    const gdpp::Type node{gdpp::TypeKind::object, "Node"};
    const gdpp::Type node_2d{gdpp::TypeKind::object, "Node2D"};

    gdpp::FlowTypeState left;
    left.refine(1, node);
    left.refine(2, node_2d);

    gdpp::FlowTypeState right;
    right.refine(1, node);
    right.refine(2, node);

    const auto joined = gdpp::FlowTypeState::join_fallthrough({&left, &right});
    REQUIRE(joined.find(1) != nullptr);
    REQUIRE_EQ(*joined.find(1), node);
    REQUIRE(joined.find(2) == nullptr);

    left.invalidate(1);
    REQUIRE(left.find(1) == nullptr);
    left.clear();
    REQUIRE(left.refinements().empty());
}

TEST_CASE("conditional refinements compose sequential and alternative paths") {
    const gdpp::Type integer{gdpp::TypeKind::integer, "int"};
    const gdpp::Type node{gdpp::TypeKind::object, "Node"};
    const gdpp::Type node_2d{gdpp::TypeKind::object, "Node2D"};

    const gdpp::TypeRefinements first{{1, node}, {2, integer}};
    const gdpp::TypeRefinements second{{1, node_2d}, {3, integer}};
    const auto sequence = gdpp::sequence_refinements(first, second);
    REQUIRE_EQ(sequence.at(1), node_2d);
    REQUIRE_EQ(sequence.at(2), integer);
    REQUIRE_EQ(sequence.at(3), integer);

    const gdpp::TypeRefinements alternative{{1, node}, {2, node_2d}};
    const auto common = gdpp::common_refinements(first, alternative);
    REQUIRE_EQ(common.size(), std::size_t{1});
    REQUIRE_EQ(common.at(1), node);
}
