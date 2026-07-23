#pragma once

#include <godot_cpp/core/error_macros.hpp>
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

} // namespace gdpp::runtime
