#pragma once

#include <cstddef>

namespace gdpp {

// Commercial defaults are intentionally generous for generated and hand-written projects while
// bounding every input-controlled allocation or recursive grammar path. Callers embedding the
// compiler may lower these values for untrusted content or raise them for audited generated code.
struct FrontendLimits {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_line_bytes{1U * 1024U * 1024U};
    std::size_t max_tokens{1'000'000U};
    std::size_t max_literal_bytes{8U * 1024U * 1024U};
    std::size_t max_indentation_depth{256U};
    std::size_t max_grouping_depth{512U};
    std::size_t max_parser_depth{512U};
    std::size_t max_diagnostics{256U};
};

} // namespace gdpp
