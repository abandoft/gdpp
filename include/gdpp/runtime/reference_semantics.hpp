#pragma once

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/variant_internal.hpp>

#include <utility>

namespace gdpp::runtime {

// godot-cpp's generated PackedArray C++ copy constructors implement the explicit
// `PackedArray(other)` copy operation. GDScript assignment and argument passing use a different
// contract: variables receive another reference to the same packed storage until duplicate() or a
// rebinding operation creates a new value. Store the value as a Variant because Variant copying is
// the engine ABI operation that retains that shared identity.
template <typename PackedArray> class SharedPackedArray final {
  public:
    SharedPackedArray() : value_(PackedArray{}) {}
    SharedPackedArray(const PackedArray& value) : value_(value) {}
    SharedPackedArray(PackedArray&& value) : value_(value) {}
    explicit SharedPackedArray(const godot::Array& value) : value_(PackedArray(value)) {}

    explicit SharedPackedArray(const godot::Variant& value) {
        if (value.get_type() ==
            static_cast<godot::Variant::Type>(
                godot::internal::VariantInternalType<PackedArray>::type)) {
            value_ = value;
            return;
        }
        ERR_PRINT("GDPP: shared packed-array storage received an incompatible Variant.");
        value_ = PackedArray{};
    }

    SharedPackedArray(const SharedPackedArray&) = default;
    SharedPackedArray(SharedPackedArray&&) noexcept = default;
    SharedPackedArray& operator=(const SharedPackedArray&) = default;
    SharedPackedArray& operator=(SharedPackedArray&&) noexcept = default;

    SharedPackedArray& operator=(const PackedArray& value) {
        value_ = godot::Variant(value);
        return *this;
    }

    SharedPackedArray& operator=(PackedArray&& value) {
        value_ = godot::Variant(value);
        return *this;
    }

    [[nodiscard]] PackedArray& native() noexcept {
        return *godot::VariantInternal::get_internal_value<PackedArray>(&value_);
    }

    [[nodiscard]] const PackedArray& native() const noexcept {
        return *godot::VariantInternal::get_internal_value<PackedArray>(&value_);
    }

    [[nodiscard]] godot::Variant& variant() noexcept { return value_; }
    [[nodiscard]] const godot::Variant& variant() const noexcept { return value_; }

    operator PackedArray&() noexcept { return native(); }
    operator const PackedArray&() const noexcept { return native(); }
    operator godot::Variant() const { return value_; }

    [[nodiscard]] bool operator!() const { return native().is_empty(); }

    friend bool operator==(const SharedPackedArray& left, const SharedPackedArray& right) {
        return left.value_ == right.value_;
    }

    friend bool operator!=(const SharedPackedArray& left, const SharedPackedArray& right) {
        return !(left == right);
    }

  private:
    godot::Variant value_;
};

template <typename PackedArray>
[[nodiscard]] SharedPackedArray<PackedArray>
packed_array_storage(const godot::Variant& value) {
    const auto expected = static_cast<godot::Variant::Type>(
        godot::internal::VariantInternalType<PackedArray>::type);
    if (value.get_type() == expected)
        return SharedPackedArray<PackedArray>(value);
    if (!godot::Variant::can_convert_strict(value.get_type(), expected)) {
        ERR_PRINT("GDPP: packed-array conversion received an incompatible Variant.");
        return {};
    }
    return SharedPackedArray<PackedArray>(static_cast<PackedArray>(value));
}

} // namespace gdpp::runtime

// Generated methods use SharedPackedArray internally, but their reflected ABI remains the exact
// Godot PackedArray Variant type. These adapters are also required for generated property
// accessors; public script functions use GDPP's Variant-call bridge so their parameter identity is
// not lost through godot-cpp's value-oriented PackedArray caster.
namespace godot {

template <typename PackedArray>
struct GetTypeInfo<gdpp::runtime::SharedPackedArray<PackedArray>> {
    static constexpr GDExtensionVariantType VARIANT_TYPE = GetTypeInfo<PackedArray>::VARIANT_TYPE;
    static constexpr GDExtensionClassMethodArgumentMetadata METADATA =
        GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

    static inline PropertyInfo get_class_info() {
        return GetTypeInfo<PackedArray>::get_class_info();
    }
};

template <typename PackedArray>
struct GetTypeInfo<const gdpp::runtime::SharedPackedArray<PackedArray>&>
    : GetTypeInfo<gdpp::runtime::SharedPackedArray<PackedArray>> {};

template <typename PackedArray>
struct VariantCaster<gdpp::runtime::SharedPackedArray<PackedArray>> {
    static _FORCE_INLINE_ gdpp::runtime::SharedPackedArray<PackedArray>
    cast(const Variant& value) {
        return gdpp::runtime::SharedPackedArray<PackedArray>(value);
    }
};

template <typename PackedArray>
struct VariantCaster<const gdpp::runtime::SharedPackedArray<PackedArray>&>
    : VariantCaster<gdpp::runtime::SharedPackedArray<PackedArray>> {};

template <typename PackedArray>
struct PtrToArg<gdpp::runtime::SharedPackedArray<PackedArray>> {
    static _FORCE_INLINE_ gdpp::runtime::SharedPackedArray<PackedArray>
    convert(const void* value) {
        return gdpp::runtime::SharedPackedArray<PackedArray>(
            *reinterpret_cast<const PackedArray*>(value));
    }

    using EncodeT = PackedArray;

    static _FORCE_INLINE_ void
    encode(const gdpp::runtime::SharedPackedArray<PackedArray>& value, void* output) {
        *reinterpret_cast<PackedArray*>(output) = value.native();
    }
};

template <typename PackedArray>
struct PtrToArg<const gdpp::runtime::SharedPackedArray<PackedArray>&>
    : PtrToArg<gdpp::runtime::SharedPackedArray<PackedArray>> {};

} // namespace godot
