#include "gdpp/semantic/intrinsics.hpp"

#include <algorithm>
#include <array>

namespace gdpp {
namespace {

constexpr auto features = std::array{
    IntrinsicFeature{IntrinsicKind::length, "len", 1, 1, IntrinsicArgumentRule::any,
                     IntrinsicResultRule::integer, "gdpp::runtime::length"},
    IntrinsicFeature{IntrinsicKind::load, "load", 1, 1, IntrinsicArgumentRule::resource_path,
                     IntrinsicResultRule::resource, "gdpp::runtime::load_resource"},
    IntrinsicFeature{IntrinsicKind::preload, "preload", 1, 1, IntrinsicArgumentRule::resource_path,
                     IntrinsicResultRule::resource, "gdpp::runtime::load_resource"},
    IntrinsicFeature{IntrinsicKind::range, "range", 1, 3, IntrinsicArgumentRule::integer,
                     IntrinsicResultRule::integer_array, "gdpp::runtime::make_range"},
};

static_assert(
    [] {
        for (std::size_t index = 1; index < features.size(); ++index) {
            if (features[index - 1].name >= features[index].name)
                return false;
        }
        return true;
    }(),
    "intrinsic registry must remain sorted by name");

} // namespace

const IntrinsicRegistry& IntrinsicRegistry::latest() noexcept {
    static const IntrinsicRegistry registry;
    return registry;
}

const IntrinsicFeature* IntrinsicRegistry::find(const std::string_view name) const noexcept {
    const auto found =
        std::lower_bound(features.begin(), features.end(), name,
                         [](const IntrinsicFeature& feature, const std::string_view candidate) {
                             return feature.name < candidate;
                         });
    return found != features.end() && found->name == name ? &*found : nullptr;
}

const IntrinsicFeature* IntrinsicRegistry::find(const IntrinsicKind kind) const noexcept {
    const auto found = std::find_if(features.begin(), features.end(),
                                    [kind](const auto& feature) { return feature.kind == kind; });
    return found == features.end() ? nullptr : &*found;
}

} // namespace gdpp
