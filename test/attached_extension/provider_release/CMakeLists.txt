cmake_minimum_required(VERSION 3.25)

project(gdpp_test_vendor_release LANGUAGES CXX)

foreach(required_variable IN ITEMS GDPP_SDK_DIR GDPP_VENDOR_SOURCE_DIR GDPP_OUTPUT_DIR
                                   GDPP_PLATFORM GDPP_ARCH)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(binding_extension ".a")
if(GDPP_PLATFORM STREQUAL "windows")
    set(binding_extension ".lib")
endif()
file(GLOB binding_libraries LIST_DIRECTORIES FALSE
    "${GDPP_SDK_DIR}/lib/*template_release*${GDPP_ARCH}*${binding_extension}")
if(GDPP_PLATFORM STREQUAL "macos" AND NOT binding_libraries)
    file(GLOB binding_libraries LIST_DIRECTORIES FALSE
        "${GDPP_SDK_DIR}/lib/*template_release*universal*${binding_extension}")
endif()
list(LENGTH binding_libraries binding_count)
if(NOT binding_count EQUAL 1)
    message(FATAL_ERROR
        "Expected one ${GDPP_PLATFORM}/${GDPP_ARCH} template_release binding; found: "
        "${binding_libraries}")
endif()

add_library(gdpp_test_vendor_release SHARED
    "${GDPP_VENDOR_SOURCE_DIR}/vendor_base.cpp"
    "${GDPP_VENDOR_SOURCE_DIR}/register_types.cpp"
)
target_compile_features(gdpp_test_vendor_release PRIVATE cxx_std_17)
target_compile_definitions(gdpp_test_vendor_release PRIVATE GDEXTENSION THREADS_ENABLED)
target_include_directories(gdpp_test_vendor_release SYSTEM PRIVATE
    "${GDPP_SDK_DIR}/godot-cpp/include"
    "${GDPP_SDK_DIR}/godot-cpp/gen/include"
)
target_include_directories(gdpp_test_vendor_release PRIVATE "${GDPP_VENDOR_SOURCE_DIR}")
target_link_libraries(gdpp_test_vendor_release PRIVATE "${binding_libraries}")

if(GDPP_PLATFORM STREQUAL "windows")
    target_compile_definitions(gdpp_test_vendor_release PRIVATE
        WINDOWS_ENABLED TYPED_METHOD_BIND _HAS_EXCEPTIONS=0 NOMINMAX)
elseif(GDPP_PLATFORM STREQUAL "macos")
    target_compile_definitions(gdpp_test_vendor_release PRIVATE MACOS_ENABLED UNIX_ENABLED)
elseif(GDPP_PLATFORM STREQUAL "linux")
    target_compile_definitions(gdpp_test_vendor_release PRIVATE LINUX_ENABLED UNIX_ENABLED)
endif()

if(MSVC)
    target_compile_options(gdpp_test_vendor_release PRIVATE
        /utf-8 /permissive- /Zc:__cplusplus /EHsc)
    target_link_options(gdpp_test_vendor_release PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
else()
    target_compile_options(gdpp_test_vendor_release PRIVATE -fno-exceptions)
endif()

set_target_properties(gdpp_test_vendor_release PROPERTIES
    OUTPUT_NAME "gdpp_test_vendor.release.${GDPP_PLATFORM}.${GDPP_ARCH}"
    LIBRARY_OUTPUT_DIRECTORY "${GDPP_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${GDPP_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${GDPP_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${GDPP_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${GDPP_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${GDPP_OUTPUT_DIR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if((GDPP_PLATFORM STREQUAL "linux") AND NOT MSVC)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vendor.exports.map"
        "{ global: gdpp_test_vendor_init; local: *; };\n")
    target_link_options(gdpp_test_vendor_release PRIVATE "LINKER:--exclude-libs,ALL")
    target_link_options(gdpp_test_vendor_release PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_BINARY_DIR}/vendor.exports.map")
elseif(GDPP_PLATFORM STREQUAL "macos")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vendor.exports.macos"
        "_gdpp_test_vendor_init\n")
    target_link_options(gdpp_test_vendor_release PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/vendor.exports.macos")
endif()
