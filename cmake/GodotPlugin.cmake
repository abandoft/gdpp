if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third/godot-cpp/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp is missing; run git submodule update --init --recursive")
endif()

set(GODOTCPP_API_VERSION "${GDPP_GODOT_API_VERSION}" CACHE STRING "" FORCE)
set(GODOTCPP_TARGET editor CACHE STRING "" FORCE)
set(GODOTCPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_SOURCE_DIR}/third/godot-cpp"
    "${CMAKE_BINARY_DIR}/third/godot-cpp"
    EXCLUDE_FROM_ALL
)

include(ExternalProject)
function(gdpp_add_sdk_binding target_name api_version godot_target output_variable)
    set(build_directory "${CMAKE_BINARY_DIR}/sdk/${target_name}")

    # The Godot target profile and the native optimizer profile are one contract.  A Debug
    # compiler plugin still has to package an optimized template_release binding; otherwise every
    # generated game crosses an -O0 godot-cpp ABI boundary and the hottest Variant/container paths
    # become slower than GDScript.  Conversely, template_debug must retain debuggable bindings even
    # when this parent build produces a commercial Release package.
    if(godot_target STREQUAL "template_release")
        set(binding_build_type Release)
    elseif(godot_target STREQUAL "template_debug")
        set(binding_build_type Debug)
    else()
        set(binding_build_type "${CMAKE_BUILD_TYPE}")
    endif()
    set(configure_arguments
        -DCMAKE_BUILD_TYPE=${binding_build_type}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DGODOTCPP_API_VERSION=${api_version}
        -DGODOTCPP_TARGET=${godot_target}
        -DGODOTCPP_ENABLE_TESTING=OFF
        -DGODOTCPP_SYSTEM_HEADERS=ON
    )
    if(CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "|" escaped_osx_architectures "${CMAKE_OSX_ARCHITECTURES}")
        list(APPEND configure_arguments
            "-DCMAKE_OSX_ARCHITECTURES=${escaped_osx_architectures}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND configure_arguments
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(WIN32)
        list(APPEND configure_arguments
            "-DCMAKE_CXX_FLAGS=/D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND configure_arguments -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()
    ExternalProject_Add(
        ${target_name}
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/third/godot-cpp"
        BINARY_DIR "${build_directory}"
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        LIST_SEPARATOR "|"
        CMAKE_ARGS ${configure_arguments}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target godot-cpp --parallel
        INSTALL_COMMAND ""
        UPDATE_COMMAND ""
    )
    set(${output_variable} "${build_directory}" PARENT_SCOPE)
endfunction()

set(GDPP_SDK_BINDING_TARGETS)
foreach(GDPP_SDK_VERSION IN LISTS GDPP_PACKAGE_GODOT_VERSIONS)
    string(REPLACE "." "_" GDPP_SDK_SUFFIX "${GDPP_SDK_VERSION}")
    set(GDPP_SDK_DEPENDENCY_VARIABLE "GDPP_SDK_BINDING_TARGETS_${GDPP_SDK_SUFFIX}")
    set(${GDPP_SDK_DEPENDENCY_VARIABLE})
    if(GDPP_SDK_VERSION STREQUAL GDPP_GODOT_API_VERSION)
        set(GDPP_SDK_EDITOR_BUILD_${GDPP_SDK_SUFFIX}
            "${CMAKE_BINARY_DIR}/third/godot-cpp")
    else()
        set(GDPP_EDITOR_TARGET "gdpp_godot_cpp_${GDPP_SDK_SUFFIX}_editor")
        gdpp_add_sdk_binding(
            "${GDPP_EDITOR_TARGET}"
            "${GDPP_SDK_VERSION}"
            editor
            "GDPP_SDK_EDITOR_BUILD_${GDPP_SDK_SUFFIX}"
        )
        list(APPEND GDPP_SDK_BINDING_TARGETS "${GDPP_EDITOR_TARGET}")
        list(APPEND ${GDPP_SDK_DEPENDENCY_VARIABLE} "${GDPP_EDITOR_TARGET}")
    endif()
    if(GDPP_SDK_VERSION STREQUAL GDPP_GODOT_API_VERSION)
        list(APPEND ${GDPP_SDK_DEPENDENCY_VARIABLE} godot-cpp)
    endif()

    set(GDPP_DEBUG_TARGET "gdpp_godot_cpp_${GDPP_SDK_SUFFIX}_debug")
    gdpp_add_sdk_binding(
        "${GDPP_DEBUG_TARGET}"
        "${GDPP_SDK_VERSION}"
        template_debug
        "GDPP_SDK_DEBUG_BUILD_${GDPP_SDK_SUFFIX}"
    )
    list(APPEND GDPP_SDK_BINDING_TARGETS "${GDPP_DEBUG_TARGET}")
    list(APPEND ${GDPP_SDK_DEPENDENCY_VARIABLE} "${GDPP_DEBUG_TARGET}")

    set(GDPP_RELEASE_TARGET "gdpp_godot_cpp_${GDPP_SDK_SUFFIX}_release")
    gdpp_add_sdk_binding(
        "${GDPP_RELEASE_TARGET}"
        "${GDPP_SDK_VERSION}"
        template_release
        "GDPP_SDK_RELEASE_BUILD_${GDPP_SDK_SUFFIX}"
    )
    list(APPEND GDPP_SDK_BINDING_TARGETS "${GDPP_RELEASE_TARGET}")
    list(APPEND ${GDPP_SDK_DEPENDENCY_VARIABLE} "${GDPP_RELEASE_TARGET}")
endforeach()

# Each entry above is a complete godot-cpp build and its nested build already uses all available
# cores. Starting several variants at once multiplies compiler memory/handle pressure without
# improving useful throughput; on MSVC this has produced orphaned build forests and dependency DB
# lock failures after one parent build exits. Keep variants sequential by default, including the
# in-tree editor binding, but leave parallelism inside each individual build untouched.
if(GDPP_SERIALIZE_SDK_BINDING_BUILDS)
    set(GDPP_PREVIOUS_SDK_BINDING_TARGET godot-cpp)
    foreach(GDPP_SDK_BINDING_TARGET IN LISTS GDPP_SDK_BINDING_TARGETS)
        add_dependencies(
            "${GDPP_SDK_BINDING_TARGET}"
            "${GDPP_PREVIOUS_SDK_BINDING_TARGET}"
        )
        set(GDPP_PREVIOUS_SDK_BINDING_TARGET "${GDPP_SDK_BINDING_TARGET}")
    endforeach()
endif()

add_library(gdpp_runtime STATIC "${CMAKE_SOURCE_DIR}/src/runtime/variant_ops.cpp")
add_library(gdpp::runtime ALIAS gdpp_runtime)
target_include_directories(gdpp_runtime PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(gdpp_runtime PUBLIC godot::cpp)
target_compile_features(gdpp_runtime PUBLIC cxx_std_17)
set_target_properties(gdpp_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
gdpp_set_project_warnings(gdpp_runtime)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" GDPP_PLATFORM)
if(GDPP_PLATFORM STREQUAL "darwin")
    set(GDPP_PLATFORM "macos")
elseif(GDPP_PLATFORM STREQUAL "emscripten")
    set(GDPP_PLATFORM "web")
endif()
if(GDPP_PLATFORM STREQUAL "windows")
    set(GDPP_PLATFORM_MINIMUM "Windows_10")
elseif(GDPP_PLATFORM STREQUAL "macos")
    set(GDPP_PLATFORM_MINIMUM "macOS_10.15")
elseif(GDPP_PLATFORM STREQUAL "linux")
    set(GDPP_PLATFORM_MINIMUM "Ubuntu_22.04")
else()
    set(GDPP_PLATFORM_MINIMUM "none")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" GDPP_ARCH)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES GDPP_OSX_ARCHITECTURE_COUNT)
    if(GDPP_OSX_ARCHITECTURE_COUNT EQUAL 2 AND
            "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES AND
            "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
        set(GDPP_ARCH "universal")
    elseif(GDPP_OSX_ARCHITECTURE_COUNT EQUAL 1 AND
            "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES)
        set(GDPP_ARCH "arm64")
    elseif(GDPP_OSX_ARCHITECTURE_COUNT EQUAL 1 AND
            "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
        set(GDPP_ARCH "x86_64")
    else()
        message(FATAL_ERROR
            "Unsupported macOS architecture set: ${CMAKE_OSX_ARCHITECTURES}")
    endif()
elseif(GDPP_ARCH MATCHES "^(aarch64|arm64)$")
    set(GDPP_ARCH "arm64")
elseif(GDPP_ARCH MATCHES "^(amd64|x86_64)$")
    set(GDPP_ARCH "x86_64")
endif()

set(GDPP_EXAMPLE_DIRECTORY "${CMAKE_SOURCE_DIR}/example")
set(GDPP_ADDON_DIRECTORY "${GDPP_EXAMPLE_DIRECTORY}/addons/gdpp")
set(GDPP_EXTENSION_OUTPUT "${GDPP_ADDON_DIRECTORY}/binary")
set(GDPP_PROJECT_BUILD_ROOT "${GDPP_ADDON_DIRECTORY}/build")
file(MAKE_DIRECTORY
    "${GDPP_ADDON_DIRECTORY}"
    "${GDPP_PROJECT_BUILD_ROOT}"
    "${GDPP_EXAMPLE_DIRECTORY}/.godot"
)

add_library(
    gdpp_godot_plugin
    SHARED
    "${CMAKE_SOURCE_DIR}/src/integration/godot/compiler_service.cpp"
    "${CMAKE_SOURCE_DIR}/src/integration/godot/register_types.cpp"
)
find_package(Threads REQUIRED)
target_link_libraries(
    gdpp_godot_plugin
    PRIVATE gdpp::core gdpp::runtime godot::cpp Threads::Threads
)
target_include_directories(
    gdpp_godot_plugin
    PRIVATE "${CMAKE_SOURCE_DIR}/src/integration/godot"
)
target_compile_features(gdpp_godot_plugin PRIVATE cxx_std_17)
set_target_properties(gdpp_godot_plugin PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
if(UNIX AND NOT APPLE)
    # godot-cpp may statically link libstdc++/libgcc for portable Linux bundles. Hide every
    # archive symbol inside this DSO: otherwise a second project GDExtension can interpose its
    # C++ runtime/locale facets and corrupt std::filesystem/string destruction in the compiler.
    target_link_options(gdpp_godot_plugin PRIVATE "LINKER:--exclude-libs,ALL")
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/gdpp/compiler.exports.map"
        CONTENT "{ global: gdpp_library_init; local: *; };\n"
    )
    target_link_options(gdpp_godot_plugin PRIVATE
        "LINKER:--version-script=${CMAKE_BINARY_DIR}/generated/gdpp/compiler.exports.map")
elseif(APPLE)
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/gdpp/compiler.exports.macos"
        CONTENT "_gdpp_library_init\n"
    )
    target_link_options(gdpp_godot_plugin PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_BINARY_DIR}/generated/gdpp/compiler.exports.macos")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        target_link_options(gdpp_godot_plugin PRIVATE "LINKER:-dead_strip" "LINKER:-x")
    elseif(UNIX)
        target_link_options(gdpp_godot_plugin PRIVATE "LINKER:-s")
    elseif(MSVC)
        target_link_options(gdpp_godot_plugin PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    endif()
endif()
target_compile_definitions(
    gdpp_godot_plugin
    PRIVATE
        GDPP_SDK_ROOT=""
        GDPP_PLATFORM="${GDPP_PLATFORM}"
        GDPP_ARCH="${GDPP_ARCH}"
)
gdpp_set_project_warnings(gdpp_godot_plugin)

set_target_properties(
    gdpp_godot_plugin
    PROPERTIES
        OUTPUT_NAME "gdpp_compiler.${GDPP_PLATFORM}.${GDPP_ARCH}"
        LIBRARY_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
        ARCHIVE_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
)

# Integration tests must load the compiler produced by the current build, never a stale
# multi-platform package artifact that happens to remain in the example project. Include a
# build-tree identity because several architecture/configuration trees may coexist and CMake
# configure must not silently rewrite another tree's descriptor.
string(SHA256 GDPP_TEST_BUILD_ID "${CMAKE_BINARY_DIR}")
string(SUBSTRING "${GDPP_TEST_BUILD_ID}" 0 12 GDPP_TEST_BUILD_ID)
set(GDPP_TEST_COMPILER_DESCRIPTOR_NAME
    "compiler_test.${GDPP_PLATFORM}.${GDPP_ARCH}.${GDPP_TEST_BUILD_ID}.gdextension")
set(GDPP_TEST_COMPILER_DESCRIPTOR
    "${GDPP_PROJECT_BUILD_ROOT}/${GDPP_TEST_COMPILER_DESCRIPTOR_NAME}")
set(GDPP_TEST_COMPILER_DESCRIPTOR_RESOURCE
    "res://addons/gdpp/build/${GDPP_TEST_COMPILER_DESCRIPTOR_NAME}")
if(APPLE AND GDPP_ARCH STREQUAL "universal")
    string(CONCAT GDPP_TEST_COMPILER_LIBRARIES
        "macos.editor.arm64 = \"res://addons/gdpp/binary/$<TARGET_FILE_NAME:gdpp_godot_plugin>\"\n"
        "macos.editor.x86_64 = \"res://addons/gdpp/binary/$<TARGET_FILE_NAME:gdpp_godot_plugin>\"")
else()
    set(GDPP_TEST_COMPILER_LIBRARIES
        "${GDPP_PLATFORM}.editor.${GDPP_ARCH} = \"res://addons/gdpp/binary/$<TARGET_FILE_NAME:gdpp_godot_plugin>\"")
endif()
file(GENERATE
    OUTPUT "${GDPP_TEST_COMPILER_DESCRIPTOR}"
    CONTENT
        "[configuration]\n\nentry_symbol = \"gdpp_library_init\"\ncompatibility_minimum = \"4.4\"\nreloadable = true\n\n[libraries]\n\n${GDPP_TEST_COMPILER_LIBRARIES}\n"
)

# A tiny no-op runtime keeps ordinary GDScript exports runnable when an AOT export preflight fails.
# On a successful export the export plugin replaces the stable descriptor with the project library.
add_library(
    gdpp_fallback
    SHARED
    "${CMAKE_SOURCE_DIR}/src/integration/godot/export_fallback.cpp"
)
target_link_libraries(gdpp_fallback PRIVATE godot::cpp)
target_compile_features(gdpp_fallback PRIVATE cxx_std_17)
set_target_properties(gdpp_fallback PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
if(UNIX AND NOT APPLE)
    target_link_options(gdpp_fallback PRIVATE "LINKER:--exclude-libs,ALL")
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/gdpp/fallback.exports.map"
        CONTENT "{ global: gdpp_export_fallback_library_init; local: *; };\n"
    )
    target_link_options(gdpp_fallback PRIVATE
        "LINKER:--version-script=${CMAKE_BINARY_DIR}/generated/gdpp/fallback.exports.map")
elseif(APPLE)
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/gdpp/fallback.exports.macos"
        CONTENT "_gdpp_export_fallback_library_init\n"
    )
    target_link_options(gdpp_fallback PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_BINARY_DIR}/generated/gdpp/fallback.exports.macos")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        target_link_options(gdpp_fallback PRIVATE "LINKER:-dead_strip" "LINKER:-x")
    elseif(UNIX)
        target_link_options(gdpp_fallback PRIVATE "LINKER:-s")
    elseif(MSVC)
        target_link_options(gdpp_fallback PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    endif()
endif()
set_target_properties(
    gdpp_fallback
    PROPERTIES
        OUTPUT_NAME "gdpp_fallback.${GDPP_PLATFORM}.${GDPP_ARCH}"
        LIBRARY_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
        ARCHIVE_OUTPUT_DIRECTORY "${GDPP_EXTENSION_OUTPUT}"
)
gdpp_set_project_warnings(gdpp_fallback)

set(GDPP_PACKAGED_SDK "${GDPP_ADDON_DIRECTORY}/sdk")
set(GDPP_PACKAGED_SDK_TARGETS)
foreach(GDPP_SDK_VERSION IN LISTS GDPP_PACKAGE_GODOT_VERSIONS)
    string(REPLACE "." "_" GDPP_SDK_SUFFIX "${GDPP_SDK_VERSION}")
    set(GDPP_SDK_DIRECTORY "${GDPP_PACKAGED_SDK}/${GDPP_SDK_VERSION}")
    set(GDPP_PACKAGED_SDK_${GDPP_SDK_SUFFIX} "${GDPP_SDK_DIRECTORY}")
    set(GDPP_SDK_MANIFEST
        "${CMAKE_BINARY_DIR}/generated/gdpp/sdk-${GDPP_SDK_VERSION}.manifest")
    file(GENERATE
        OUTPUT "${GDPP_SDK_MANIFEST}"
        CONTENT
            "GDPP_SDK ${GDPP_NATIVE_SDK_SCHEMA}\napi ${GDPP_SDK_VERSION}\nplatform ${GDPP_PLATFORM}\narch ${GDPP_ARCH}\nprofiles development,debug,release\nplatform_minimum ${GDPP_PLATFORM_MINIMUM}\ngdpp_version ${PROJECT_VERSION}\nruntime_abi ${GDPP_NATIVE_RUNTIME_ABI}\nruntime_header_sha256 ${GDPP_NATIVE_RUNTIME_HEADER_SHA256}\nruntime_source_sha256 ${GDPP_NATIVE_RUNTIME_SOURCE_SHA256}\ncompiler ${CMAKE_CXX_COMPILER_ID}\ncompiler_version ${CMAKE_CXX_COMPILER_VERSION}\n"
    )

    set(GDPP_SDK_PACKAGE_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${GDPP_SDK_DIRECTORY}/include/gdpp/runtime"
                "${GDPP_SDK_DIRECTORY}/src/runtime"
                "${GDPP_SDK_DIRECTORY}/godot-cpp/gen"
                "${GDPP_SDK_DIRECTORY}/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${GDPP_NATIVE_RUNTIME_HEADER}"
                "${GDPP_SDK_DIRECTORY}/include/gdpp/runtime/variant_ops.hpp"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${GDPP_NATIVE_RUNTIME_SOURCE}"
                "${GDPP_SDK_DIRECTORY}/src/runtime/variant_ops.cpp"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/third/godot-cpp/include"
                "${GDPP_SDK_DIRECTORY}/godot-cpp/include"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third/godot-cpp/LICENSE.md"
                "${GDPP_SDK_DIRECTORY}/godot-cpp/LICENSE.md"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${GDPP_SDK_EDITOR_BUILD_${GDPP_SDK_SUFFIX}}/gen/include"
                "${GDPP_SDK_DIRECTORY}/godot-cpp/gen/include"
    )
    if(GDPP_SDK_VERSION STREQUAL GDPP_GODOT_API_VERSION)
        list(APPEND GDPP_SDK_PACKAGE_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:godot-cpp>"
                    "${GDPP_SDK_DIRECTORY}/lib"
        )
    else()
        list(APPEND GDPP_SDK_PACKAGE_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${GDPP_SDK_EDITOR_BUILD_${GDPP_SDK_SUFFIX}}/bin"
                    "${GDPP_SDK_DIRECTORY}/lib"
        )
    endif()
    list(APPEND GDPP_SDK_PACKAGE_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${GDPP_SDK_DEBUG_BUILD_${GDPP_SDK_SUFFIX}}/bin"
                "${GDPP_SDK_DIRECTORY}/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${GDPP_SDK_RELEASE_BUILD_${GDPP_SDK_SUFFIX}}/bin"
                "${GDPP_SDK_DIRECTORY}/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${GDPP_SDK_MANIFEST}"
                "${GDPP_SDK_DIRECTORY}/sdk.manifest"
    )
    set(GDPP_SDK_DEPENDENCY_VARIABLE "GDPP_SDK_BINDING_TARGETS_${GDPP_SDK_SUFFIX}")
    set(GDPP_PACKAGED_SDK_TARGET "gdpp_packaged_sdk_${GDPP_SDK_SUFFIX}")
    add_custom_target(
        ${GDPP_PACKAGED_SDK_TARGET}
        # Host SDK rebuilds and independently installed target packs share the version root.
        # Remove only host-owned paths so packaging macOS/Windows/Linux can never erase Android.
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${GDPP_SDK_DIRECTORY}/include"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${GDPP_SDK_DIRECTORY}/src"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${GDPP_SDK_DIRECTORY}/godot-cpp"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${GDPP_SDK_DIRECTORY}/lib"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${GDPP_SDK_DIRECTORY}/sdk.manifest"
        ${GDPP_SDK_PACKAGE_COMMANDS}
        DEPENDS
            ${${GDPP_SDK_DEPENDENCY_VARIABLE}}
            "${GDPP_SDK_MANIFEST}"
            "${CMAKE_SOURCE_DIR}/third/godot-cpp/LICENSE.md"
            "${GDPP_NATIVE_RUNTIME_HEADER}"
            "${GDPP_NATIVE_RUNTIME_SOURCE}"
        COMMENT "Packaging compiler-only Godot ${GDPP_SDK_VERSION} SDK"
        VERBATIM
    )
    list(APPEND GDPP_PACKAGED_SDK_TARGETS "${GDPP_PACKAGED_SDK_TARGET}")
endforeach()

add_custom_target(
    gdpp_packaged_sdk ALL
    DEPENDS ${GDPP_PACKAGED_SDK_TARGETS}
)

set(GDPP_SMOKE_DIR "${CMAKE_BINARY_DIR}/generated-smoke")
add_custom_command(
    OUTPUT
        "${GDPP_SMOKE_DIR}/hello_aot.gd.hpp"
        "${GDPP_SMOKE_DIR}/hello_aot.gd.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GDPP_SMOKE_DIR}"
    COMMAND $<TARGET_FILE:gdpp> compile "${CMAKE_SOURCE_DIR}/example/hello.gd"
            --output "${GDPP_SMOKE_DIR}"
    DEPENDS gdpp "${CMAKE_SOURCE_DIR}/example/hello.gd"
    VERBATIM
)
add_library(
    gdpp_generated_smoke
    STATIC
    "${GDPP_SMOKE_DIR}/hello_aot.gd.cpp"
)
target_include_directories(gdpp_generated_smoke PRIVATE "${GDPP_SMOKE_DIR}")
target_link_libraries(gdpp_generated_smoke PRIVATE gdpp::runtime godot::cpp)
target_compile_features(gdpp_generated_smoke PRIVATE cxx_std_17)
gdpp_set_project_warnings(gdpp_generated_smoke)

set(GDPP_PROJECT_SMOKE_ROOT "${GDPP_EXAMPLE_DIRECTORY}")
set(GDPP_PROJECT_SMOKE_OUTPUT "${GDPP_PROJECT_BUILD_ROOT}/project")
list(GET GDPP_PACKAGE_GODOT_VERSIONS 0 GDPP_PROJECT_SMOKE_VERSION)
string(REPLACE "." "_" GDPP_PROJECT_SMOKE_SUFFIX "${GDPP_PROJECT_SMOKE_VERSION}")
set(GDPP_PROJECT_SMOKE_SDK "${GDPP_PACKAGED_SDK_${GDPP_PROJECT_SMOKE_SUFFIX}}")
configure_file(
    "${CMAKE_SOURCE_DIR}/test/native_project/smoke.gd"
    "${GDPP_PROJECT_BUILD_ROOT}/native_smoke.gd"
    COPYONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/test/native_project/extension_list.cfg"
    "${GDPP_PROJECT_SMOKE_ROOT}/.godot/extension_list.cfg"
    COPYONLY
)
add_custom_command(
    OUTPUT
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/example_autoload_state.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/example_autoload_state.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_a.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_a.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_b.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_b.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/hello_aot.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/hello_aot.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_base.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_base.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_child.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_child.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/native_project_player.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/native_project_player.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/required_initializer.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/required_initializer.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/register_types.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/CMakeLists.txt"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/prune_stale_development.cmake"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/gdpp_project.gdextension"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/build_id.txt"
    COMMAND $<TARGET_FILE:gdpp> project "${GDPP_PROJECT_SMOKE_ROOT}"
            --output "${GDPP_PROJECT_SMOKE_OUTPUT}"
            --sdk-root "${GDPP_PROJECT_SMOKE_SDK}"
            --godot-cpp "${CMAKE_SOURCE_DIR}/third/godot-cpp"
            --target-godot "${GDPP_PROJECT_SMOKE_VERSION}"
    DEPENDS
        gdpp
        gdpp_packaged_sdk
        "${CMAKE_SOURCE_DIR}/example/autoload_state.gd"
        "${CMAKE_SOURCE_DIR}/example/cross_ref_a.gd"
        "${CMAKE_SOURCE_DIR}/example/cross_ref_b.gd"
        "${CMAKE_SOURCE_DIR}/example/hello.gd"
        "${CMAKE_SOURCE_DIR}/example/inheritance_base.gd"
        "${CMAKE_SOURCE_DIR}/example/inheritance_child.gd"
        "${CMAKE_SOURCE_DIR}/example/player.gd"
        "${CMAKE_SOURCE_DIR}/example/required_initializer.gd"
    VERBATIM
)
add_custom_target(
    gdpp_project_sources ALL
    DEPENDS
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/example_autoload_state.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/example_autoload_state.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_a.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_a.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_b.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/cross_ref_b.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/hello_aot.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/hello_aot.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_base.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_base.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_child.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/inheritance_child.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/native_project_player.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/native_project_player.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/required_initializer.gd.hpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/generated/required_initializer.gd.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/register_types.cpp"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/CMakeLists.txt"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/prune_stale_development.cmake"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/gdpp_project.gdextension"
        "${GDPP_PROJECT_SMOKE_OUTPUT}/build_id.txt"
)

add_custom_target(
    gdpp_addon ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "${GDPP_EXTENSION_OUTPUT}/libgdpp.${GDPP_PLATFORM}.editor.${GDPP_ARCH}.dylib"
            "${GDPP_EXTENSION_OUTPUT}/libgdpp.${GDPP_PLATFORM}.editor.${GDPP_ARCH}.so"
            "${GDPP_EXTENSION_OUTPUT}/gdpp.${GDPP_PLATFORM}.editor.${GDPP_ARCH}.dll"
            "${GDPP_EXTENSION_OUTPUT}/libgdpp.${GDPP_PLATFORM}.template_release.${GDPP_ARCH}.dylib"
            "${GDPP_EXTENSION_OUTPUT}/libgdpp.${GDPP_PLATFORM}.template_release.${GDPP_ARCH}.so"
            "${GDPP_EXTENSION_OUTPUT}/gdpp.${GDPP_PLATFORM}.template_release.${GDPP_ARCH}.dll"
    DEPENDS gdpp_godot_plugin gdpp_fallback gdpp_packaged_sdk
    VERBATIM
)

configure_file(
    "${CMAKE_SOURCE_DIR}/test/godot/smoke.gd"
    "${GDPP_PROJECT_BUILD_ROOT}/service_smoke.gd"
    COPYONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/test/godot/direct_build.gd"
    "${GDPP_PROJECT_BUILD_ROOT}/direct_build.gd"
    COPYONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/test/godot/direct_build_4_7.gd"
    "${GDPP_PROJECT_BUILD_ROOT}/direct_build_4_7.gd"
    COPYONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/test/godot/direct_export_build.gd"
    "${GDPP_PROJECT_BUILD_ROOT}/direct_export_build.gd"
    COPYONLY
)

if(GDPP_BUILD_TESTS AND EXISTS "${GDPP_GODOT_EXECUTABLE}")
    add_test(
        NAME gdpp.godot.reset_extension_registry
        COMMAND "${CMAKE_COMMAND}"
                -DGDPP_PROJECT_DIRECTORY=${GDPP_EXAMPLE_DIRECTORY}
                -DGDPP_COMPILER_DESCRIPTOR=${GDPP_TEST_COMPILER_DESCRIPTOR_RESOURCE}
                -DGDPP_INCLUDE_PROJECT_EXTENSION=OFF
                -P "${CMAKE_SOURCE_DIR}/test/native_project/write_extension_registry.cmake"
    )
    set_tests_properties(
        gdpp.godot.reset_extension_registry
        PROPERTIES FIXTURES_SETUP gdpp_clean_extension_registry
    )
    add_test(
        NAME gdpp.godot.restore_extension_registry
        COMMAND "${CMAKE_COMMAND}"
                -DGDPP_PROJECT_DIRECTORY=${GDPP_EXAMPLE_DIRECTORY}
                -DGDPP_COMPILER_DESCRIPTOR=res://addons/gdpp/gdpp.gdextension
                -DGDPP_INCLUDE_PROJECT_EXTENSION=OFF
                -P "${CMAKE_SOURCE_DIR}/test/native_project/write_extension_registry.cmake"
    )
    set_tests_properties(
        gdpp.godot.restore_extension_registry
        PROPERTIES FIXTURES_CLEANUP gdpp_clean_extension_registry
    )
    add_test(
        NAME gdpp.godot.service
        COMMAND "${GDPP_GODOT_EXECUTABLE}" --headless --path "${GDPP_EXAMPLE_DIRECTORY}"
                --script addons/gdpp/build/service_smoke.gd
    )
    set_tests_properties(
        gdpp.godot.service
        PROPERTIES
            PASS_REGULAR_EXPRESSION "GDPP_SMOKE_OK"
            FIXTURES_REQUIRED gdpp_clean_extension_registry
            TIMEOUT 60
    )
    add_test(
        NAME gdpp.godot.direct_build
        COMMAND "${GDPP_GODOT_EXECUTABLE}" --headless --path "${GDPP_EXAMPLE_DIRECTORY}"
                --script addons/gdpp/build/direct_build.gd
    )
    set_tests_properties(
        gdpp.godot.direct_build
        PROPERTIES
            PASS_REGULAR_EXPRESSION "GDPP_DIRECT_BUILD_OK"
            FIXTURES_SETUP gdpp_direct_native
            FIXTURES_REQUIRED "gdpp_cmake_scaffold;gdpp_clean_extension_registry"
            TIMEOUT 600
    )
    add_test(
        NAME gdpp.godot.register_native_project
        COMMAND "${CMAKE_COMMAND}"
                -DGDPP_PROJECT_DIRECTORY=${GDPP_EXAMPLE_DIRECTORY}
                -DGDPP_COMPILER_DESCRIPTOR=${GDPP_TEST_COMPILER_DESCRIPTOR_RESOURCE}
                -DGDPP_INCLUDE_PROJECT_EXTENSION=ON
                -P "${CMAKE_SOURCE_DIR}/test/native_project/write_extension_registry.cmake"
    )
    set_tests_properties(
        gdpp.godot.register_native_project
        PROPERTIES
            FIXTURES_REQUIRED "gdpp_direct_native;gdpp_clean_extension_registry"
            FIXTURES_SETUP gdpp_native_extension_registry
    )
    add_test(
        NAME gdpp.godot.native_project
        COMMAND "${GDPP_GODOT_EXECUTABLE}" --headless --path "${GDPP_PROJECT_SMOKE_ROOT}"
                --script addons/gdpp/build/native_smoke.gd
    )
    set_tests_properties(
        gdpp.godot.native_project
        PROPERTIES
            PASS_REGULAR_EXPRESSION "GDPP_NATIVE_PROJECT_OK"
            FIXTURES_REQUIRED "gdpp_native_extension_registry;gdpp_clean_extension_registry"
            TIMEOUT 120
    )
    if("4.7" IN_LIST GDPP_PACKAGE_GODOT_VERSIONS)
        add_test(
            NAME gdpp.godot.direct_build_4_7
            COMMAND "${GDPP_GODOT_EXECUTABLE}" --headless --path "${GDPP_EXAMPLE_DIRECTORY}"
                    --script addons/gdpp/build/direct_build_4_7.gd
        )
        set_tests_properties(
            gdpp.godot.direct_build_4_7
            PROPERTIES
                PASS_REGULAR_EXPRESSION "GDPP_DIRECT_BUILD_4_7_OK"
                DEPENDS gdpp.godot.native_project
                FIXTURES_REQUIRED gdpp_clean_extension_registry
                TIMEOUT 600
        )
        set(GDPP_DIRECT_EXPORT_DEPENDENCY gdpp.godot.direct_build_4_7)
    else()
        set(GDPP_DIRECT_EXPORT_DEPENDENCY gdpp.godot.native_project)
    endif()
    add_test(
        NAME gdpp.godot.direct_export_build
        COMMAND "${GDPP_GODOT_EXECUTABLE}" --headless --path "${GDPP_EXAMPLE_DIRECTORY}"
                --script addons/gdpp/build/direct_export_build.gd
    )
    set_tests_properties(
        gdpp.godot.direct_export_build
        PROPERTIES
            PASS_REGULAR_EXPRESSION "GDPP_DIRECT_EXPORT_BUILD_OK"
            DEPENDS ${GDPP_DIRECT_EXPORT_DEPENDENCY}
            FIXTURES_REQUIRED gdpp_clean_extension_registry
            TIMEOUT 600
    )
endif()

if(GDPP_BUILD_TESTS)
    add_test(
        NAME gdpp.project.cmake.configure
        COMMAND "${CMAKE_COMMAND}"
                -S "${GDPP_PROJECT_SMOKE_OUTPUT}"
                -B "${GDPP_PROJECT_SMOKE_OUTPUT}/configure-test"
                -G Ninja
                -DGODOTCPP_TARGET=editor
                -DGDPP_GODOT_CPP_DIR=${CMAKE_SOURCE_DIR}/third/godot-cpp
    )
    set_tests_properties(
        gdpp.project.cmake.configure
        PROPERTIES FIXTURES_SETUP gdpp_cmake_scaffold
    )
endif()
