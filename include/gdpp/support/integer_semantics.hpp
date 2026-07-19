#pragma once

#include <cstdint>
#include <limits>

namespace gdpp::integer {

using Value = std::int64_t;
using Bits = std::uint64_t;

inline constexpr unsigned width = std::numeric_limits<Bits>::digits;
inline constexpr Bits sign_bit = Bits{1} << (width - 1U);
inline constexpr Bits shift_mask = width - 1U;

static_assert(width == 64U, "GDScript integers require an exact 64-bit unsigned carrier");
static_assert(std::numeric_limits<Value>::digits == 63,
              "GDScript integers require an exact 64-bit signed carrier");

// C++17 does not have std::bit_cast and converting an out-of-range unsigned value to a signed
// value is implementation-defined. This arithmetic mapping preserves the exact two's-complement
// bit pattern without relying on either behavior.
[[nodiscard]] constexpr Value from_bits(const Bits bits) noexcept {
    if (bits <= static_cast<Bits>(std::numeric_limits<Value>::max()))
        return static_cast<Value>(bits);
    return Value{-1} - static_cast<Value>(std::numeric_limits<Bits>::max() - bits);
}

[[nodiscard]] constexpr Bits to_bits(const Value value) noexcept {
    return static_cast<Bits>(value);
}

[[nodiscard]] constexpr Value add(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) + to_bits(right));
}

[[nodiscard]] constexpr Value subtract(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) - to_bits(right));
}

[[nodiscard]] constexpr Value multiply(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) * to_bits(right));
}

[[nodiscard]] constexpr Value negate(const Value operand) noexcept {
    return from_bits(Bits{0} - to_bits(operand));
}

[[nodiscard]] constexpr Value bit_not(const Value operand) noexcept {
    return from_bits(~to_bits(operand));
}

[[nodiscard]] constexpr Value bit_and(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) & to_bits(right));
}

[[nodiscard]] constexpr Value bit_or(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) | to_bits(right));
}

[[nodiscard]] constexpr Value bit_xor(const Value left, const Value right) noexcept {
    return from_bits(to_bits(left) ^ to_bits(right));
}

[[nodiscard]] constexpr unsigned normalized_shift(const Value count) noexcept {
    return static_cast<unsigned>(to_bits(count) & shift_mask);
}

[[nodiscard]] constexpr Value shift_left(const Value value, const Value count) noexcept {
    return from_bits(to_bits(value) << normalized_shift(count));
}

[[nodiscard]] constexpr Value shift_right(const Value value, const Value count) noexcept {
    const auto shift = normalized_shift(count);
    if (shift == 0U)
        return value;
    const auto bits = to_bits(value);
    const auto shifted = bits >> shift;
    if ((bits & sign_bit) == 0U)
        return from_bits(shifted);
    return from_bits(shifted | (~Bits{0} << (width - shift)));
}

// A mathematical range terminates at its exclusive stop. If the next fixed-width value would
// wrap against the requested direction, clamp directly to that stop so a C++ for-loop cannot
// become infinite at INT64_MIN/INT64_MAX. The caller validates that step is nonzero.
[[nodiscard]] constexpr Value range_advance(const Value value, const Value step,
                                            const Value stop) noexcept {
    const auto next = add(value, step);
    if ((step > 0 && next <= value) || (step < 0 && next >= value))
        return stop;
    return next;
}

enum class ArithmeticError : std::uint8_t {
    none,
    division_by_zero,
    modulo_by_zero,
};

struct Result {
    Value value{0};
    ArithmeticError error{ArithmeticError::none};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == ArithmeticError::none;
    }
};

[[nodiscard]] constexpr Result divide(const Value left, const Value right) noexcept {
    if (right == 0)
        return {0, ArithmeticError::division_by_zero};
    if (left == std::numeric_limits<Value>::min() && right == -1)
        return {std::numeric_limits<Value>::min(), ArithmeticError::none};
    return {left / right, ArithmeticError::none};
}

[[nodiscard]] constexpr Result modulo(const Value left, const Value right) noexcept {
    if (right == 0)
        return {0, ArithmeticError::modulo_by_zero};
    if (left == std::numeric_limits<Value>::min() && right == -1)
        return {0, ArithmeticError::none};
    return {left % right, ArithmeticError::none};
}

} // namespace gdpp::integer
