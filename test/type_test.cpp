#include "support/test.hpp"

#include "gdpp/semantic/conversion.hpp"

#include <array>
#include <set>
#include <utility>

namespace {

using Pair = std::pair<gdpp::VariantType, gdpp::VariantType>;

constexpr std::array all_variant_types{
    gdpp::VariantType::nil,
    gdpp::VariantType::boolean,
    gdpp::VariantType::integer,
    gdpp::VariantType::floating,
    gdpp::VariantType::string,
    gdpp::VariantType::vector2,
    gdpp::VariantType::vector2i,
    gdpp::VariantType::rect2,
    gdpp::VariantType::rect2i,
    gdpp::VariantType::vector3,
    gdpp::VariantType::vector3i,
    gdpp::VariantType::transform2d,
    gdpp::VariantType::vector4,
    gdpp::VariantType::vector4i,
    gdpp::VariantType::plane,
    gdpp::VariantType::quaternion,
    gdpp::VariantType::aabb,
    gdpp::VariantType::basis,
    gdpp::VariantType::transform3d,
    gdpp::VariantType::projection,
    gdpp::VariantType::color,
    gdpp::VariantType::string_name,
    gdpp::VariantType::node_path,
    gdpp::VariantType::rid,
    gdpp::VariantType::object,
    gdpp::VariantType::callable,
    gdpp::VariantType::signal,
    gdpp::VariantType::dictionary,
    gdpp::VariantType::array,
    gdpp::VariantType::packed_byte_array,
    gdpp::VariantType::packed_int32_array,
    gdpp::VariantType::packed_int64_array,
    gdpp::VariantType::packed_float32_array,
    gdpp::VariantType::packed_float64_array,
    gdpp::VariantType::packed_string_array,
    gdpp::VariantType::packed_vector2_array,
    gdpp::VariantType::packed_vector3_array,
    gdpp::VariantType::packed_color_array,
    gdpp::VariantType::packed_vector4_array,
};

gdpp::Type type_for(const gdpp::VariantType type) {
    if (type == gdpp::VariantType::nil)
        return {gdpp::TypeKind::nil, "null"};
    if (type == gdpp::VariantType::object)
        return {gdpp::TypeKind::object, "Object"};
    return gdpp::type_from_annotation(std::string{gdpp::variant_type_name(type)});
}

std::set<Pair> strict_pairs() {
    std::set<Pair> result;
    for (const auto type : all_variant_types)
        result.emplace(type, type);
    const auto add = [&](const gdpp::VariantType target,
                         const std::initializer_list<gdpp::VariantType> sources) {
        for (const auto source : sources)
            result.emplace(target, source);
    };
    add(gdpp::VariantType::object, {gdpp::VariantType::nil});
    add(gdpp::VariantType::boolean,
        {gdpp::VariantType::integer, gdpp::VariantType::floating});
    add(gdpp::VariantType::integer,
        {gdpp::VariantType::boolean, gdpp::VariantType::floating});
    add(gdpp::VariantType::floating,
        {gdpp::VariantType::boolean, gdpp::VariantType::integer});
    add(gdpp::VariantType::string,
        {gdpp::VariantType::node_path, gdpp::VariantType::string_name});
    add(gdpp::VariantType::vector2, {gdpp::VariantType::vector2i});
    add(gdpp::VariantType::vector2i, {gdpp::VariantType::vector2});
    add(gdpp::VariantType::rect2, {gdpp::VariantType::rect2i});
    add(gdpp::VariantType::rect2i, {gdpp::VariantType::rect2});
    add(gdpp::VariantType::vector3, {gdpp::VariantType::vector3i});
    add(gdpp::VariantType::vector3i, {gdpp::VariantType::vector3});
    add(gdpp::VariantType::transform2d, {gdpp::VariantType::transform3d});
    add(gdpp::VariantType::vector4, {gdpp::VariantType::vector4i});
    add(gdpp::VariantType::vector4i, {gdpp::VariantType::vector4});
    add(gdpp::VariantType::quaternion, {gdpp::VariantType::basis});
    add(gdpp::VariantType::basis, {gdpp::VariantType::quaternion});
    add(gdpp::VariantType::transform3d,
        {gdpp::VariantType::transform2d, gdpp::VariantType::quaternion,
         gdpp::VariantType::basis, gdpp::VariantType::projection});
    add(gdpp::VariantType::projection, {gdpp::VariantType::transform3d});
    add(gdpp::VariantType::color,
        {gdpp::VariantType::string, gdpp::VariantType::integer});
    add(gdpp::VariantType::string_name, {gdpp::VariantType::string});
    add(gdpp::VariantType::node_path, {gdpp::VariantType::string});
    add(gdpp::VariantType::rid, {gdpp::VariantType::object});
    add(gdpp::VariantType::array,
        {gdpp::VariantType::packed_byte_array, gdpp::VariantType::packed_int32_array,
         gdpp::VariantType::packed_int64_array, gdpp::VariantType::packed_float32_array,
         gdpp::VariantType::packed_float64_array, gdpp::VariantType::packed_string_array,
         gdpp::VariantType::packed_vector2_array, gdpp::VariantType::packed_vector3_array,
         gdpp::VariantType::packed_color_array, gdpp::VariantType::packed_vector4_array});
    for (const auto packed : {gdpp::VariantType::packed_byte_array,
                              gdpp::VariantType::packed_int32_array,
                              gdpp::VariantType::packed_int64_array,
                              gdpp::VariantType::packed_float32_array,
                              gdpp::VariantType::packed_float64_array,
                              gdpp::VariantType::packed_string_array,
                              gdpp::VariantType::packed_vector2_array,
                              gdpp::VariantType::packed_vector3_array,
                              gdpp::VariantType::packed_color_array,
                              gdpp::VariantType::packed_vector4_array}) {
        add(packed, {gdpp::VariantType::array});
    }
    return result;
}

} // namespace

TEST_CASE("semantic types preserve null, zero and void as separate domains") {
    const gdpp::Type variant{gdpp::TypeKind::variant, "Variant"};
    const gdpp::Type object{gdpp::TypeKind::object, "Object"};
    const gdpp::Type nil{gdpp::TypeKind::nil, "null"};
    const gdpp::Type array{gdpp::TypeKind::array, "Array"};
    const gdpp::Type string{gdpp::TypeKind::string, "String"};
    const gdpp::Type void_type{gdpp::TypeKind::void_type, "void"};

    REQUIRE(variant.accepts_null());
    REQUIRE(object.accepts_null());
    REQUIRE(nil.accepts_null());
    REQUIRE(!array.accepts_null());
    REQUIRE(!string.accepts_null());
    REQUIRE(!void_type.accepts_null());
    REQUIRE(!void_type.is_value());
    REQUIRE_EQ(nil.truthiness(), gdpp::TruthinessKind::always_false);
    REQUIRE_EQ(array.truthiness(), gdpp::TruthinessKind::zero_value);
    REQUIRE_EQ(object.truthiness(), gdpp::TruthinessKind::object_validity);
    REQUIRE_EQ(void_type.truthiness(), gdpp::TruthinessKind::invalid);
}

TEST_CASE("Variant type mapping covers the complete Godot 4 value domain") {
    std::set<std::string_view> names;
    for (const auto variant_type : all_variant_types) {
        const auto type = type_for(variant_type);
        REQUIRE(gdpp::variant_type_of(type).has_value());
        REQUIRE_EQ(*gdpp::variant_type_of(type), variant_type);
        names.insert(gdpp::variant_type_name(variant_type));
    }
    REQUIRE_EQ(names.size(), all_variant_types.size());
    REQUIRE(!gdpp::variant_type_of({gdpp::TypeKind::variant, "Variant"}).has_value());
    REQUIRE(!gdpp::variant_type_of({gdpp::TypeKind::void_type, "void"}).has_value());
}

TEST_CASE("implicit conversion matrix exactly mirrors Godot strict Variant conversions") {
    const auto expected = strict_pairs();
    for (const auto target : all_variant_types) {
        for (const auto source : all_variant_types) {
            REQUIRE_EQ(gdpp::is_implicitly_convertible(type_for(target), type_for(source)),
                       expected.find({target, source}) != expected.end());
        }
    }
}

TEST_CASE("explicit conversion matrix adds only Godot permissive casts") {
    auto expected = strict_pairs();
    expected.emplace(gdpp::VariantType::boolean, gdpp::VariantType::string);
    expected.emplace(gdpp::VariantType::integer, gdpp::VariantType::string);
    expected.emplace(gdpp::VariantType::floating, gdpp::VariantType::string);
    for (const auto source : all_variant_types) {
        if (source != gdpp::VariantType::nil && source != gdpp::VariantType::object)
            expected.emplace(gdpp::VariantType::string, source);
    }

    for (const auto target : all_variant_types) {
        for (const auto source : all_variant_types) {
            REQUIRE_EQ(gdpp::is_explicitly_convertible(type_for(target), type_for(source)),
                       expected.find({target, source}) != expected.end());
        }
    }
}

TEST_CASE("typed containers are invariant while typed and untyped boundaries remain dynamic") {
    const gdpp::Type integers{gdpp::TypeKind::array, "Array[int]"};
    const gdpp::Type floats{gdpp::TypeKind::array, "Array[float]"};
    const gdpp::Type untyped_array{gdpp::TypeKind::array, "Array"};
    const gdpp::Type scores{gdpp::TypeKind::dictionary, "Dictionary[String, int]"};
    const gdpp::Type weights{gdpp::TypeKind::dictionary, "Dictionary[String, float]"};
    const gdpp::Type untyped_dictionary{gdpp::TypeKind::dictionary, "Dictionary"};

    REQUIRE(!gdpp::is_implicitly_convertible(integers, floats));
    REQUIRE(!gdpp::is_explicitly_convertible(integers, floats));
    REQUIRE(gdpp::is_implicitly_convertible(integers, untyped_array));
    REQUIRE(gdpp::is_implicitly_convertible(untyped_array, integers));
    REQUIRE(!gdpp::is_implicitly_convertible(scores, weights));
    REQUIRE(gdpp::is_implicitly_convertible(scores, untyped_dictionary));
    REQUIRE(gdpp::is_implicitly_convertible(untyped_dictionary, scores));
}

TEST_CASE("dynamic conversion never makes void or null value types assignable") {
    const gdpp::Type variant{gdpp::TypeKind::variant, "Variant"};
    const gdpp::Type integer{gdpp::TypeKind::integer, "int"};
    const gdpp::Type array{gdpp::TypeKind::array, "Array"};
    const gdpp::Type nil{gdpp::TypeKind::nil, "null"};
    const gdpp::Type object{gdpp::TypeKind::object, "Object"};
    const gdpp::Type void_type{gdpp::TypeKind::void_type, "void"};

    REQUIRE_EQ(gdpp::classify_conversion(integer, variant), gdpp::ConversionKind::dynamic);
    REQUIRE(gdpp::is_implicitly_convertible(variant, integer));
    REQUIRE(gdpp::is_implicitly_convertible(object, nil));
    REQUIRE(!gdpp::is_implicitly_convertible(array, nil));
    REQUIRE(!gdpp::is_implicitly_convertible(integer, void_type));
    REQUIRE(!gdpp::is_implicitly_convertible(void_type, integer));
}
