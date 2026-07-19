#include "gdpp/semantic/type.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace gdpp {

bool Type::is_numeric() const noexcept {
    return kind == TypeKind::integer || kind == TypeKind::floating || kind == TypeKind::enumeration;
}

bool Type::is_dynamic() const noexcept {
    return kind == TypeKind::unknown || kind == TypeKind::variant;
}

bool Type::is_packed_array() const noexcept {
    if (kind != TypeKind::builtin)
        return false;
    static const std::unordered_set<std::string> packed_arrays{
        "PackedByteArray",    "PackedInt32Array",   "PackedInt64Array",   "PackedFloat32Array",
        "PackedFloat64Array", "PackedStringArray",  "PackedVector2Array", "PackedVector3Array",
        "PackedColorArray",   "PackedVector4Array",
    };
    return packed_arrays.find(name) != packed_arrays.end();
}

std::string Type::display_name() const {
    if (!name.empty())
        return name;
    switch (kind) {
    case TypeKind::unknown:
        return "unknown";
    case TypeKind::variant:
        return "Variant";
    case TypeKind::nil:
        return "null";
    case TypeKind::boolean:
        return "bool";
    case TypeKind::integer:
        return "int";
    case TypeKind::floating:
        return "float";
    case TypeKind::string:
        return "String";
    case TypeKind::string_name:
        return "StringName";
    case TypeKind::array:
        return "Array";
    case TypeKind::dictionary:
        return "Dictionary";
    case TypeKind::enumeration:
        return name.empty() ? "enum" : name;
    case TypeKind::script_resource:
        return name.empty() ? "Script" : "Script[" + name + "]";
    case TypeKind::builtin:
        return "builtin";
    case TypeKind::object:
        return "Object";
    case TypeKind::void_type:
        return "void";
    }
    return "unknown";
}

Type type_from_annotation(const std::string& annotation) {
    static const std::unordered_map<std::string, TypeKind> builtins{
        {"Variant", TypeKind::variant}, {"bool", TypeKind::boolean},
        {"int", TypeKind::integer},     {"float", TypeKind::floating},
        {"String", TypeKind::string},   {"StringName", TypeKind::string_name},
        {"Array", TypeKind::array},     {"Dictionary", TypeKind::dictionary},
        {"void", TypeKind::void_type},
    };
    if (annotation.rfind("Array[", 0) == 0)
        return {TypeKind::array, annotation};
    if (annotation.rfind("Dictionary[", 0) == 0)
        return {TypeKind::dictionary, annotation};
    const auto found = builtins.find(annotation);
    if (found != builtins.end())
        return {found->second, annotation};
    static const std::unordered_set<std::string> value_types{
        "Vector2",
        "Vector2i",
        "Rect2",
        "Rect2i",
        "Vector3",
        "Vector3i",
        "Transform2D",
        "Vector4",
        "Vector4i",
        "Plane",
        "Quaternion",
        "AABB",
        "Basis",
        "Transform3D",
        "Projection",
        "Color",
        "NodePath",
        "RID",
        "Callable",
        "Signal",
        "PackedByteArray",
        "PackedInt32Array",
        "PackedInt64Array",
        "PackedFloat32Array",
        "PackedFloat64Array",
        "PackedStringArray",
        "PackedVector2Array",
        "PackedVector3Array",
        "PackedColorArray",
        "PackedVector4Array",
    };
    if (value_types.find(annotation) != value_types.end()) {
        return {TypeKind::builtin, annotation};
    }
    return {TypeKind::object, annotation};
}

Type packed_array_element_type(const Type& packed_array) {
    static const std::unordered_map<std::string, Type> elements{
        {"PackedByteArray", {TypeKind::integer, "int"}},
        {"PackedInt32Array", {TypeKind::integer, "int"}},
        {"PackedInt64Array", {TypeKind::integer, "int"}},
        {"PackedFloat32Array", {TypeKind::floating, "float"}},
        {"PackedFloat64Array", {TypeKind::floating, "float"}},
        {"PackedStringArray", {TypeKind::string, "String"}},
        {"PackedVector2Array", {TypeKind::builtin, "Vector2"}},
        {"PackedVector3Array", {TypeKind::builtin, "Vector3"}},
        {"PackedColorArray", {TypeKind::builtin, "Color"}},
        {"PackedVector4Array", {TypeKind::builtin, "Vector4"}},
    };
    const auto found = elements.find(packed_array.name);
    return found == elements.end() ? Type{TypeKind::variant, "Variant"} : found->second;
}

std::optional<std::vector<std::string>>
generic_type_arguments(const std::string_view type_name, const std::string_view container_name,
                       const std::size_t expected_arguments) {
    if (type_name.size() <= container_name.size() + 2U ||
        type_name.compare(0, container_name.size(), container_name) != 0 ||
        type_name[container_name.size()] != '[' || type_name.back() != ']') {
        return std::nullopt;
    }
    const auto trim = [](std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
        return value;
    };
    auto content =
        type_name.substr(container_name.size() + 1U, type_name.size() - container_name.size() - 2U);
    std::vector<std::string> arguments;
    std::size_t begin = 0;
    std::size_t depth = 0;
    for (std::size_t index = 0; index <= content.size(); ++index) {
        const bool at_end = index == content.size();
        const auto character = at_end ? '\0' : content[index];
        if (!at_end && character == '[') {
            ++depth;
            continue;
        }
        if (!at_end && character == ']') {
            if (depth == 0)
                return std::nullopt;
            --depth;
            continue;
        }
        if (!at_end && (character != ',' || depth != 0))
            continue;
        const auto argument = trim(content.substr(begin, index - begin));
        if (argument.empty())
            return std::nullopt;
        arguments.emplace_back(argument);
        begin = index + 1U;
    }
    if (depth != 0 || arguments.size() != expected_arguments)
        return std::nullopt;
    return arguments;
}

bool ContainerTypeDescriptor::has_runtime_constraint() const noexcept {
    return std::any_of(arguments.begin(), arguments.end(),
                       [](const std::string& argument) { return argument != "Variant"; });
}

std::optional<ContainerTypeDescriptor> describe_container_type(const Type& type) {
    if (type.kind == TypeKind::array) {
        const auto arguments = generic_type_arguments(type.name, "Array", 1);
        if (arguments)
            return ContainerTypeDescriptor{ContainerTypeKind::array, *arguments};
    }
    if (type.kind == TypeKind::dictionary) {
        const auto arguments = generic_type_arguments(type.name, "Dictionary", 2);
        if (arguments)
            return ContainerTypeDescriptor{ContainerTypeKind::dictionary, *arguments};
    }
    return std::nullopt;
}

bool is_explicitly_typed_container(const Type& type) {
    return describe_container_type(type).has_value();
}

bool is_assignable(const Type& target, const Type& source) noexcept {
    if (target.is_dynamic() || source.is_dynamic())
        return true;
    if (target == source)
        return true;
    if (target.kind == TypeKind::floating && source.kind == TypeKind::integer)
        return true;
    if (target.kind == TypeKind::integer && source.kind == TypeKind::floating)
        return true;
    if (target.kind == TypeKind::integer && source.kind == TypeKind::boolean)
        return true;
    if (target.kind == TypeKind::string_name && source.kind == TypeKind::string)
        return true;
    if (target.kind == TypeKind::string && source.kind == TypeKind::string_name)
        return true;
    if (source.kind == TypeKind::nil &&
        (target.kind == TypeKind::object || target.kind == TypeKind::array ||
         target.kind == TypeKind::dictionary || target.kind == TypeKind::string ||
         target.kind == TypeKind::string_name)) {
        return true;
    }
    if (target.kind == TypeKind::array && source.kind == TypeKind::array) {
        const bool target_typed = target.name.rfind("Array[", 0) == 0;
        const bool source_typed = source.name.rfind("Array[", 0) == 0;
        return !target_typed || !source_typed || target.name == source.name;
    }
    if (target.kind == TypeKind::array && source.is_packed_array())
        return true;
    if (target.kind == TypeKind::dictionary && source.kind == TypeKind::dictionary) {
        const bool target_typed = target.name.rfind("Dictionary[", 0) == 0;
        const bool source_typed = source.name.rfind("Dictionary[", 0) == 0;
        return !target_typed || !source_typed || target.name == source.name;
    }
    if (target.kind == TypeKind::enumeration &&
        (source.kind == TypeKind::integer ||
         (source.kind == TypeKind::enumeration && target.name == source.name)))
        return true;
    if (target.kind == TypeKind::script_resource && source.kind == TypeKind::script_resource)
        return target.name == source.name;
    if (target.kind == TypeKind::integer && source.kind == TypeKind::enumeration)
        return true;
    if (target.kind == TypeKind::builtin && source.kind == TypeKind::builtin) {
        return target.name == source.name ||
               (target.name == "Vector2" && source.name == "Vector2i") ||
               (target.name == "Vector3" && source.name == "Vector3i") ||
               (target.name == "Vector4" && source.name == "Vector4i") ||
               (target.name == "Vector2i" && source.name == "Vector2") ||
               (target.name == "Vector3i" && source.name == "Vector3") ||
               (target.name == "Vector4i" && source.name == "Vector4");
    }
    if (target.kind == TypeKind::object && source.kind == TypeKind::object) {
        return target.name == source.name || target.name == "Object";
    }
    return false;
}

} // namespace gdpp
