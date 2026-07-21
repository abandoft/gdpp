#include "vendor_base.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize_vendor(const godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(gdpp::test::VendorBase);
        GDREGISTER_CLASS(gdpp::test::VendorResource);
    }
}

void uninitialize_vendor(const godot::ModuleInitializationLevel) {}

} // namespace

extern "C" GDExtensionBool GDE_EXPORT gdpp_test_vendor_init(
    GDExtensionInterfaceGetProcAddress address, GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init{address, library, initialization};
    init.register_initializer(initialize_vendor);
    init.register_terminator(uninitialize_vendor);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
