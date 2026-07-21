set(runtime_descriptor "${GDPP_TEST_SOURCE_DIR}/example/addons/gdpp/gdpp_project.gdextension")
if(EXISTS "${runtime_descriptor}")
    message(FATAL_ERROR
        "The runtime descriptor must be generated into the export, not scanned beside the compiler")
endif()

file(READ "${GDPP_TEST_SOURCE_DIR}/example/addons/gdpp/plugin.cfg" plugin_metadata)
foreach(required_plugin_metadata IN ITEMS
        "name=\"GDPP\""
        "description=\"GDScript AOT & Extension\""
        "author=\"GodotHub\""
        "website=\"https://godothub.com\""
        "version=\"1.7.0\""
        "script=\"plugin.gd\"")
    string(FIND "${plugin_metadata}" "${required_plugin_metadata}" metadata_offset)
    if(metadata_offset EQUAL -1)
        message(FATAL_ERROR
            "Plugin metadata is missing: ${required_plugin_metadata}")
    endif()
endforeach()

set(compiler_descriptor
    "${GDPP_TEST_SOURCE_DIR}/example/addons/gdpp/gdpp.gdextension")
file(READ "${compiler_descriptor}" compiler_content)
set(compiler_entries "macos.editor.arm64" "macos.editor.x86_64")
set(compiler_libraries
    "libgdpp_compiler.macos.arm64.dylib"
    "libgdpp_compiler.macos.universal.dylib")
foreach(entry library IN ZIP_LISTS compiler_entries compiler_libraries)
    string(FIND "${compiler_content}"
        "${entry} = \"res://addons/gdpp/binary/${library}\"" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR
            "Compiler descriptor does not map ${entry} to ${library}")
    endif()
endforeach()
string(REGEX MATCH "macos\\.[^=\n]*universal[^=\n]* =" invalid_compiler_feature
    "${compiler_content}")
if(invalid_compiler_feature)
    message(FATAL_ERROR
        "Compiler descriptor uses 'universal' as a Godot feature tag: "
        "${invalid_compiler_feature}")
endif()

file(READ "${GDPP_TEST_SOURCE_DIR}/example/addons/gdpp/export_plugin.gd" export_plugin)
file(READ "${GDPP_TEST_SOURCE_DIR}/src/integration/godot/compiler_service.cpp" compiler_service)
string(FIND "${compiler_service}"
    "output[\"attached_script_bases\"] = attached_script_bases;"
    attached_metadata_offset)
if(attached_metadata_offset EQUAL -1)
    message(FATAL_ERROR
        "Compiler service does not expose third-party attachment metadata to the exporter")
endif()
string(FIND "${compiler_service}"
    "output[\"editor_only_scripts\"] = editor_only_scripts;"
    editor_only_metadata_offset)
if(editor_only_metadata_offset EQUAL -1)
    message(FATAL_ERROR
        "Compiler service does not expose editor-only script metadata to the exporter")
endif()
string(FIND "${export_plugin}" "add_shared_object(\n        library_path" duplicate_registration)
if(NOT duplicate_registration EQUAL -1)
    message(FATAL_ERROR
        "Successful exports must not register a library already discovered through GDExtension")
endif()
string(FIND "${export_plugin}"
    "_prepare_compiler_descriptor() or not _prepare_stable_descriptor()"
    duplicate_descriptor_scan)
if(NOT duplicate_descriptor_scan EQUAL -1)
    message(FATAL_ERROR
        "Compiler and runtime descriptors must not both discover the project library")
endif()
foreach(required_runtime_export IN ITEMS
        "if not _prepare_compiler_descriptor():"
        "add_file(COMPILER_DESCRIPTOR, _runtime_descriptor.to_utf8_buffer(), false)"
        "add_file(EXTENSION_REGISTRY, _export_extension_registry.to_utf8_buffer(), false)"
        "func _clear_godot_export_cache() -> bool:"
        "func _restore_compiler_descriptor() -> void:")
    string(FIND "${export_plugin}" "${required_runtime_export}" runtime_export_offset)
    if(runtime_export_offset EQUAL -1)
        message(FATAL_ERROR
            "Single-descriptor export transaction is missing: ${required_runtime_export}")
    endif()
endforeach()
foreach(required_scene_compatibility IN ITEMS
        "current.has_method(&\"get_base_scene_state\")"
        "current.call(&\"get_base_scene_state\") as SceneState")
    string(FIND "${export_plugin}" "${required_scene_compatibility}" compatibility_offset)
    if(compatibility_offset EQUAL -1)
        message(FATAL_ERROR
            "Godot 4.4 scene-state compatibility guard is missing: "
            "${required_scene_compatibility}")
    endif()
endforeach()
foreach(required_abstract_contract IN ITEMS
        "distribution_result.get(\"abstract_scripts\", PackedStringArray())"
        "not _abstract_scripts.has(script_path)"
        "and not ClassDB.can_instantiate(native_class_name)")
    string(FIND "${export_plugin}" "${required_abstract_contract}" abstract_contract_offset)
    if(abstract_contract_offset EQUAL -1)
        message(FATAL_ERROR
            "Abstract native export validation is missing: ${required_abstract_contract}")
    endif()
endforeach()
foreach(required_editor_only_contract IN ITEMS
        "\"editor_only_scripts\", PackedStringArray()"
        "if _editor_only_scripts.has(script_path):"
        "autoload '%s' uses an editor-only script"
        "runtime scene '%s' uses editor-only script '%s'"
        "runtime resource graph '%s' uses editor-only script '%s'")
    string(FIND "${export_plugin}" "${required_editor_only_contract}" editor_only_contract_offset)
    if(editor_only_contract_offset EQUAL -1)
        message(FATAL_ERROR
            "Editor-only runtime export guard is missing: ${required_editor_only_contract}")
    endif()
endforeach()
foreach(required_attached_contract IN ITEMS
        "distribution_result.get(\"attached_script_bases\", {})"
        "ClassDB.class_exists(&\"AttachedCompiledScript\")"
        "func _install_attached_script("
        "object.set_script(script)"
        "[sub_resource type=\\\"AttachedCompiledScript\\\" id=\\\"AttachedScript\\\"]")
    string(FIND "${export_plugin}" "${required_attached_contract}" attached_contract_offset)
    if(attached_contract_offset EQUAL -1)
        message(FATAL_ERROR
            "Third-party GDExtension export attachment is missing: ${required_attached_contract}")
    endif()
endforeach()
file(READ "${GDPP_TEST_SOURCE_DIR}/example/addons/gdpp/plugin.gd" editor_plugin)
string(FIND "${editor_plugin}"
    "if not DirAccess.dir_exists_absolute(ndk_parent):" safe_ndk_probe)
if(safe_ndk_probe EQUAL -1)
    message(FATAL_ERROR
        "Optional Android NDK discovery must not query a missing directory")
endif()
file(READ "${GDPP_TEST_SOURCE_DIR}/example/export_presets.cfg" export_presets)
foreach(forbidden_export_minimum IN ITEMS
        "gradle_build/min_sdk="
        "application/min_ios_version="
        "min_macos_version")
    string(FIND "${export_presets}" "${forbidden_export_minimum}" minimum_offset)
    if(NOT minimum_offset EQUAL -1)
        message(FATAL_ERROR
            "Export fixtures must inherit the official template minimum instead of "
            "overriding it: ${forbidden_export_minimum}")
    endif()
endforeach()

foreach(invalid_android_option IN ITEMS
        "gradle_build/compress_native_libraries=true"
        "gradle_build/target_sdk=\"35\""
        "package/signed=true")
    string(FIND "${export_presets}" "${invalid_android_option}" invalid_offset)
    if(NOT invalid_offset EQUAL -1)
        message(FATAL_ERROR
            "Non-Gradle unsigned Android fixture contains invalid option: ${invalid_android_option}")
    endif()
endforeach()
foreach(required_windows_option IN ITEMS
        "name=\"Windows x86_64\""
        "name=\"Windows GDScript Fallback\""
        "platform=\"Windows Desktop\""
        "export_path=\"addons/gdpp/build/export/GDPPExample.exe\""
        "export_path=\"addons/gdpp/build/export/GDPPFallback.exe\"")
    string(FIND "${export_presets}" "${required_windows_option}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR "Windows export fixture is missing: ${required_windows_option}")
    endif()
endforeach()

foreach(required_macos_option IN ITEMS
        "name=\"macOS Universal\""
        "name=\"macOS GDScript Fallback\""
        "export_path=\"addons/gdpp/build/export/GDPPFallback.app\"")
    string(FIND "${export_presets}" "${required_macos_option}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR "macOS export fixture is missing: ${required_macos_option}")
    endif()
endforeach()

foreach(required_web_option IN ITEMS
        "name=\"Web AOT\""
        "name=\"Web AOT Threads\""
        "name=\"Web GDScript Fallback\""
        "platform=\"Web\""
        "variant/extensions_support=true"
        "variant/thread_support=true"
        "export_path=\"addons/gdpp/build/export/web/GDPPExample.html\"")
    string(FIND "${export_presets}" "${required_web_option}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR "Web export fixture is missing: ${required_web_option}")
    endif()
endforeach()

foreach(required_web_implementation IN ITEMS
        "func _web_variant_from_features"
        "variant/extensions_support"
        "EMSCRIPTEN_CXX_SETTING"
        "\"threads\" if _target_platform == \"web\"")
    string(FIND "${export_plugin}" "${required_web_implementation}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR
            "Web export implementation is missing: ${required_web_implementation}")
    endif()
endforeach()

foreach(required_ios_option IN ITEMS
        "name=\"iOS AOT\""
        "name=\"iOS GDScript Fallback\""
        "platform=\"iOS\""
        "application/export_project_only=true"
        "architectures/arm64=true"
        "export_path=\"addons/gdpp/build/export/ios/GDPPExample\"")
    string(FIND "${export_presets}" "${required_ios_option}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR "iOS export fixture is missing: ${required_ios_option}")
    endif()
endforeach()
foreach(required_ios_implementation IN ITEMS
        "func _ios_compiler() -> String:"
        "func _native_artifact_exists(path: String) -> bool:"
        "\"macos\", \"windows\", \"linux\", \"android\", \"ios\", \"web\""
        "\"ios\": \"xcframework\"")
    string(FIND "${export_plugin}" "${required_ios_implementation}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR
            "iOS export implementation is missing: ${required_ios_implementation}")
    endif()
endforeach()
