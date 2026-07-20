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

// Null is a value in GDScript, while void is the absence of a value. Built-in value types and
// containers are never nullable: their default-constructed/empty values must not be confused with
// null merely because they are false in a boolean context.
enum class Nullability {
    non_nullable,
    nullable,
    null_only,
    not_a_value,
};

// Every GDScript value can participate in a boolean context. The classification is deliberately
// independent from nullability because empty strings and containers are false without being null.
enum class TruthinessKind {
    invalid,
    dynamic_value,
    always_false,
    zero_value,
    object_validity,
};

// A dependency-free mirror of Godot 4's stable Variant::Type domain. Keeping it in the compiler
// core lets semantic conversion decisions and generated runtime conversions share one vocabulary
// without linking the compiler executable to godot-cpp.
enum class VariantType {
    nil,
    boolean,
    integer,
    floating,
    string,
    vector2,
    vector2i,
    rect2,
    rect2i,
    vector3,
    vector3i,
    transform2d,
    vector4,
    vector4i,
    plane,
    quaternion,
    aabb,
    basis,
    transform3d,
    projection,
    color,
    string_name,
    node_path,
    rid,
    object,
    callable,
    signal,
    dictionary,
    array,
    packed_byte_array,
    packed_int32_array,
    packed_int64_array,
    packed_float32_array,
    packed_float64_array,
    packed_string_array,
    packed_vector2_array,
    packed_vector3_array,
    packed_color_array,
    packed_vector4_array,
};

struct Type {
    TypeKind kind{TypeKind::unknown};
    std::string name;

    [[nodiscard]] bool is_numeric() const noexcept;
    [[nodiscard]] bool is_dynamic() const noexcept;
    [[nodiscard]] bool is_packed_array() const noexcept;
    [[nodiscard]] Nullability nullability() const noexcept;
    [[nodiscard]] TruthinessKind truthiness() const noexcept;
    [[nodiscard]] bool accepts_null() const noexcept;
    [[nodiscard]] bool is_value() const noexcept;
    [[nodiscard]] std::string display_name() const;

    friend bool operator==(const Type& left, const Type& right) noexcept {
        return left.kind == right.kind && left.name == right.name;
    }
    friend bool operator!=(const Type& left, const Type& right) noexcept {
        return !(left == right);
    }
};

enum class ContainerTypeKind { array, dictionary };

struct ContainerTypeDescriptor {
    ContainerTypeKind kind{ContainerTypeKind::array};
    std::vector<std::string> arguments;

    [[nodiscard]] bool has_runtime_constraint() const noexcept;
};

[[nodiscard]] Type type_from_annotation(const std::string& annotation);
[[nodiscard]] std::optional<VariantType> variant_type_of(const Type& type) noexcept;
[[nodiscard]] std::string_view variant_type_name(VariantType type) noexcept;
[[nodiscard]] Type packed_array_element_type(const Type& packed_array);
[[nodiscard]] std::optional<std::vector<std::string>>
generic_type_arguments(std::string_view type_name, std::string_view container_name,
                       std::size_t expected_arguments);
[[nodiscard]] std::optional<ContainerTypeDescriptor> describe_container_type(const Type& type);
[[nodiscard]] bool is_explicitly_typed_container(const Type& type);
[[nodiscard]] bool is_assignable(const Type& target, const Type& source) noexcept;

} // namespace gdpp
