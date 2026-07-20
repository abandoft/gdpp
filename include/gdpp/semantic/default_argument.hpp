#pragma once

#include <cstdint>

namespace gdpp {

// Godot accepts both compile-time constants and expressions that must be evaluated when an
// omitted argument enters the callable. Keep that distinction explicit past semantic analysis so
// later IR and backend passes cannot accidentally cache a mutable or instance-dependent default.
enum class DefaultArgumentEvaluation : std::uint8_t {
    absent,
    compile_time_constant,
    call_time,
};

} // namespace gdpp
