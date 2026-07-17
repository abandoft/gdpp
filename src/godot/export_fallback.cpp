#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>

namespace {

void initialize_fallback(void*, GDExtensionInitializationLevel) {}
void uninitialize_fallback(void*, GDExtensionInitializationLevel) {}

} // namespace

extern "C" GDExtensionBool GDE_EXPORT
gdpp_export_fallback_library_init(GDExtensionInterfaceGetProcAddress, GDExtensionClassLibraryPtr,
                                  GDExtensionInitialization* initialization) {
    initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    initialization->userdata = nullptr;
    initialization->initialize = initialize_fallback;
    initialization->deinitialize = uninitialize_fallback;
    return true;
}
