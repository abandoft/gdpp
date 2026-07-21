#include "vendor_base.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace gdpp::test {

void VendorBase::set_native_bias(const std::int64_t value) { native_bias_ = value; }

std::int64_t VendorBase::get_native_bias() const { return native_bias_; }

std::int64_t VendorBase::native_compute(const std::int64_t value) const {
    return value + native_bias_;
}

std::int64_t VendorBase::invoke_hook(const std::int64_t value) {
    return static_cast<std::int64_t>(call("hook", value));
}

void VendorBase::emit_vendor_ping(const std::int64_t value) { emit_signal("vendor_ping", value); }

std::int64_t VendorBase::get_ready_notifications() const { return ready_notifications_; }

void VendorBase::_notification(const std::int32_t what) {
    if (what == NOTIFICATION_READY)
        ++ready_notifications_;
}

void VendorBase::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_native_bias", "value"),
                                &VendorBase::set_native_bias);
    godot::ClassDB::bind_method(godot::D_METHOD("get_native_bias"), &VendorBase::get_native_bias);
    godot::ClassDB::bind_method(godot::D_METHOD("native_compute", "value"),
                                &VendorBase::native_compute);
    godot::ClassDB::bind_method(godot::D_METHOD("invoke_hook", "value"), &VendorBase::invoke_hook);
    godot::ClassDB::bind_method(godot::D_METHOD("emit_vendor_ping", "value"),
                                &VendorBase::emit_vendor_ping);
    godot::ClassDB::bind_method(godot::D_METHOD("get_ready_notifications"),
                                &VendorBase::get_ready_notifications);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "native_bias"), "set_native_bias",
                 "get_native_bias");
    ADD_SIGNAL(godot::MethodInfo("vendor_ping", godot::PropertyInfo(godot::Variant::INT, "value")));
}

void VendorResource::set_native_bias(const std::int64_t value) { native_bias_ = value; }

std::int64_t VendorResource::get_native_bias() const { return native_bias_; }

std::int64_t VendorResource::native_compute(const std::int64_t value) const {
    return value + native_bias_;
}

void VendorResource::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_native_bias", "value"),
                                &VendorResource::set_native_bias);
    godot::ClassDB::bind_method(godot::D_METHOD("get_native_bias"),
                                &VendorResource::get_native_bias);
    godot::ClassDB::bind_method(godot::D_METHOD("native_compute", "value"),
                                &VendorResource::native_compute);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "native_bias"), "set_native_bias",
                 "get_native_bias");
}

} // namespace gdpp::test
