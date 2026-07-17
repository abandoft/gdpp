#include "gdpp/semantic/type.hpp"

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
    if (target.kind == TypeKind::array && source.kind == TypeKind::array)
        return true;
    if (target.kind == TypeKind::array && source.is_packed_array())
        return true;
    if (target.kind == TypeKind::dictionary && source.kind == TypeKind::dictionary)
        return true;
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
