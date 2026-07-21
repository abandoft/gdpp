#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>

#include <cstdint>

namespace gdpp::test {

class VendorBase final : public godot::Node {
    GDCLASS(VendorBase, godot::Node)

  public:
    void set_native_bias(std::int64_t value);
    [[nodiscard]] std::int64_t get_native_bias() const;
    [[nodiscard]] std::int64_t native_compute(std::int64_t value) const;
    [[nodiscard]] std::int64_t invoke_hook(std::int64_t value);
    void emit_vendor_ping(std::int64_t value);
    [[nodiscard]] std::int64_t get_ready_notifications() const;
    void _notification(std::int32_t what);

  protected:
    static void _bind_methods();

  private:
    std::int64_t native_bias_{5};
    std::int64_t ready_notifications_{0};
};

class VendorResource final : public godot::Resource {
    GDCLASS(VendorResource, godot::Resource)

  public:
    void set_native_bias(std::int64_t value);
    [[nodiscard]] std::int64_t get_native_bias() const;
    [[nodiscard]] std::int64_t native_compute(std::int64_t value) const;

  protected:
    static void _bind_methods();

  private:
    std::int64_t native_bias_{3};
};

} // namespace gdpp::test
