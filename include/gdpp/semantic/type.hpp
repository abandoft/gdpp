#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdpp {

enum class TypeKind {
    unknown,
    variant,
    nil,
    boolean,
    integer,
    floating,
    string,
    string_name,
    array,
    dictionary,
    enumeration,
    script_resource,
    builtin,
    object,
    void_type,
};

struct Type {
    TypeKind kind{TypeKind::unknown};
    std::string name;

    [[nodiscard]] bool is_numeric() const noexcept;
    [[nodiscard]] bool is_dynamic() const noexcept;
    [[nodiscard]] bool is_packed_array() const noexcept;
    [[nodiscard]] std::string display_name() const;

    friend bool operator==(const Type& left, const Type& right) noexcept {
        return left.kind == right.kind && left.name == right.name;
    }
    friend bool operator!=(const Type& left, const Type& right) noexcept {
        return !(left == right);
    }
};

[[nodiscard]] Type type_from_annotation(const std::string& annotation);
[[nodiscard]] Type packed_array_element_type(const Type& packed_array);
[[nodiscard]] std::optional<std::vector<std::string>>
generic_type_arguments(std::string_view type_name, std::string_view container_name,
                       std::size_t expected_arguments);
[[nodiscard]] bool is_assignable(const Type& target, const Type& source) noexcept;

} // namespace gdpp
