#include "support/test.hpp"

#include "gdpp/numeric/integer_semantics.hpp"

#include <cstdint>
#include <limits>

TEST_CASE("integer semantics preserve every signed bit pattern portably") {
    constexpr auto minimum = std::numeric_limits<gdpp::integer::Value>::min();
    constexpr auto maximum = std::numeric_limits<gdpp::integer::Value>::max();

    static_assert(gdpp::integer::from_bits(gdpp::integer::sign_bit) == minimum);
    static_assert(gdpp::integer::from_bits(~gdpp::integer::Bits{0}) == -1);
    static_assert(gdpp::integer::to_bits(minimum) == gdpp::integer::sign_bit);

    REQUIRE_EQ(gdpp::integer::from_bits(gdpp::integer::to_bits(minimum)), minimum);
    REQUIRE_EQ(gdpp::integer::from_bits(gdpp::integer::to_bits(maximum)), maximum);
    REQUIRE_EQ(gdpp::integer::from_bits(gdpp::integer::to_bits(-1)), std::int64_t{-1});
}

TEST_CASE("integer arithmetic has deterministic modulo two-to-the-sixty-four behavior") {
    constexpr auto minimum = std::numeric_limits<gdpp::integer::Value>::min();
    constexpr auto maximum = std::numeric_limits<gdpp::integer::Value>::max();

    REQUIRE_EQ(gdpp::integer::add(maximum, 1), minimum);
    REQUIRE_EQ(gdpp::integer::subtract(minimum, 1), maximum);
    REQUIRE_EQ(gdpp::integer::multiply(maximum, 2), std::int64_t{-2});
    REQUIRE_EQ(gdpp::integer::multiply(minimum, -1), minimum);
    REQUIRE_EQ(gdpp::integer::negate(minimum), minimum);
    REQUIRE_EQ(gdpp::integer::bit_not(0), std::int64_t{-1});
    REQUIRE_EQ(gdpp::integer::bit_and(-1, 0x55AA), std::int64_t{0x55AA});
    REQUIRE_EQ(gdpp::integer::bit_or(0x5500, 0x00AA), std::int64_t{0x55AA});
    REQUIRE_EQ(gdpp::integer::bit_xor(0x55FF, 0x0055), std::int64_t{0x55AA});
}

TEST_CASE("integer shifts normalize counts and define arithmetic right shift") {
    constexpr auto minimum = std::numeric_limits<gdpp::integer::Value>::min();
    constexpr auto maximum = std::numeric_limits<gdpp::integer::Value>::max();

    REQUIRE_EQ(gdpp::integer::normalized_shift(64), 0U);
    REQUIRE_EQ(gdpp::integer::normalized_shift(-1), 63U);
    REQUIRE_EQ(gdpp::integer::shift_left(1, 63), minimum);
    REQUIRE_EQ(gdpp::integer::shift_left(maximum, 1), std::int64_t{-2});
    REQUIRE_EQ(gdpp::integer::shift_left(1, 64), std::int64_t{1});
    REQUIRE_EQ(gdpp::integer::shift_left(1, -1), minimum);
    REQUIRE_EQ(gdpp::integer::shift_right(minimum, 1), minimum / 2);
    REQUIRE_EQ(gdpp::integer::shift_right(-1, 1), std::int64_t{-1});
    REQUIRE_EQ(gdpp::integer::shift_right(maximum, 64), maximum);
}

TEST_CASE("integer division reports only zero divisors and contains the signed overflow edge") {
    constexpr auto minimum = std::numeric_limits<gdpp::integer::Value>::min();

    const auto divide_zero = gdpp::integer::divide(7, 0);
    REQUIRE(!divide_zero);
    REQUIRE_EQ(divide_zero.error, gdpp::integer::ArithmeticError::division_by_zero);
    const auto modulo_zero = gdpp::integer::modulo(7, 0);
    REQUIRE(!modulo_zero);
    REQUIRE_EQ(modulo_zero.error, gdpp::integer::ArithmeticError::modulo_by_zero);

    const auto division_edge = gdpp::integer::divide(minimum, -1);
    REQUIRE(static_cast<bool>(division_edge));
    REQUIRE_EQ(division_edge.value, minimum);
    const auto modulo_edge = gdpp::integer::modulo(minimum, -1);
    REQUIRE(static_cast<bool>(modulo_edge));
    REQUIRE_EQ(modulo_edge.value, std::int64_t{0});
    REQUIRE_EQ(gdpp::integer::divide(-7, 3).value, std::int64_t{-2});
    REQUIRE_EQ(gdpp::integer::modulo(-7, 3).value, std::int64_t{-1});
}

TEST_CASE("integer range advancement terminates instead of wrapping at signed limits") {
    constexpr auto minimum = std::numeric_limits<gdpp::integer::Value>::min();
    constexpr auto maximum = std::numeric_limits<gdpp::integer::Value>::max();

    REQUIRE_EQ(gdpp::integer::range_advance(maximum - 1, 2, maximum), maximum);
    REQUIRE_EQ(gdpp::integer::range_advance(minimum + 1, -2, minimum), minimum);
    REQUIRE_EQ(gdpp::integer::range_advance(7, 3, 20), std::int64_t{10});
    REQUIRE_EQ(gdpp::integer::range_advance(-7, -3, -20), std::int64_t{-10});
}
