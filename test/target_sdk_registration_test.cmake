if(NOT DEFINED GDPP_TEST_SOURCE_DIR OR NOT DEFINED GDPP_TEST_BINARY_DIR)
    message(FATAL_ERROR "Target SDK registration test requires source and binary directories")
endif()

set(GDPP_TEST_STAGE "${GDPP_TEST_BINARY_DIR}/target-sdk-registration")
file(REMOVE_RECURSE "${GDPP_TEST_STAGE}")
set(GDPP_TEST_INPUT
    "${GDPP_TEST_SOURCE_DIR}/third/godot-cpp/include/godot_cpp/core/class_db.hpp")
set(GDPP_TEST_OUTPUT
    "${GDPP_TEST_STAGE}/godot-cpp/include/godot_cpp/core/class_db.hpp")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DGDPP_CLASS_DB_INPUT=${GDPP_TEST_INPUT}"
        "-DGDPP_CLASS_DB_OUTPUT=${GDPP_TEST_OUTPUT}"
        -P "${GDPP_TEST_SOURCE_DIR}/cmake/PatchGodotCppClassDB.cmake"
    RESULT_VARIABLE GDPP_TEST_PATCH_STATUS
)
if(NOT GDPP_TEST_PATCH_STATUS EQUAL 0)
    message(FATAL_ERROR "Target SDK ClassDB patch failed")
endif()
file(READ "${GDPP_TEST_OUTPUT}" GDPP_TEST_PATCHED_HEADER)
foreach(GDPP_TEST_CONTRACT IN ITEMS
        "static void register_runtime_abstract_class();"
        "void ClassDB::register_runtime_abstract_class()")
    string(FIND "${GDPP_TEST_PATCHED_HEADER}" "${GDPP_TEST_CONTRACT}" GDPP_TEST_OFFSET)
    if(GDPP_TEST_OFFSET EQUAL -1)
        message(FATAL_ERROR "Patched target SDK header is missing: ${GDPP_TEST_CONTRACT}")
    endif()
endforeach()

foreach(GDPP_TEST_PACKAGER IN ITEMS build_android_sdk.py build_web_sdk.py build_ios_sdk.py)
    file(READ "${GDPP_TEST_SOURCE_DIR}/tools/${GDPP_TEST_PACKAGER}" GDPP_TEST_PACKAGER_SOURCE)
    foreach(GDPP_TEST_CONTRACT IN ITEMS
            "class_db_patch = source_root / \"cmake/PatchGodotCppClassDB.cmake\""
            "-DGDPP_CLASS_DB_INPUT="
            "-DGDPP_CLASS_DB_OUTPUT="
            "str(class_db_patch)"
            "cxx_standard 17"
            "exceptions disabled"
            "msvc_runtime not_applicable")
        string(FIND "${GDPP_TEST_PACKAGER_SOURCE}" "${GDPP_TEST_CONTRACT}" GDPP_TEST_OFFSET)
        if(GDPP_TEST_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "${GDPP_TEST_PACKAGER} does not preserve the ClassDB contract: "
                "${GDPP_TEST_CONTRACT}")
        endif()
    endforeach()
endforeach()

# Both independent CMake consumers of packaged godot-cpp archives must inherit one centralized
# parent toolchain contract: the SDK profile builders and the release vendor GDExtension fixture.
file(READ "${GDPP_TEST_SOURCE_DIR}/cmake/GodotPlugin.cmake" GDPP_TEST_PLUGIN_CMAKE)
foreach(GDPP_TEST_HOST_ABI_FIELD IN ITEMS
        "cxx_standard 17"
        "exceptions disabled"
        "msvc_runtime \${GDPP_SDK_MSVC_RUNTIME}")
    string(FIND "${GDPP_TEST_PLUGIN_CMAKE}"
        "${GDPP_TEST_HOST_ABI_FIELD}"
        GDPP_TEST_HOST_ABI_FIELD_OFFSET)
    if(GDPP_TEST_HOST_ABI_FIELD_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Host SDK manifest omits ${GDPP_TEST_HOST_ABI_FIELD}")
    endif()
endforeach()
foreach(GDPP_TEST_CONTRACT_FIELD IN ITEMS
        CMAKE_CXX_COMPILER
        CMAKE_TOOLCHAIN_FILE
        CMAKE_SYSROOT
        CMAKE_CXX_COMPILER_TARGET
        CMAKE_OSX_DEPLOYMENT_TARGET
        CMAKE_OSX_SYSROOT
        CMAKE_MSVC_RUNTIME_LIBRARY
        CMAKE_RC_COMPILER
        CMAKE_MT)
    string(FIND "${GDPP_TEST_PLUGIN_CMAKE}"
        "            ${GDPP_TEST_CONTRACT_FIELD}"
        GDPP_TEST_CONTRACT_FIELD_OFFSET)
    if(GDPP_TEST_CONTRACT_FIELD_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Central native CMake contract omits ${GDPP_TEST_CONTRACT_FIELD}")
    endif()
endforeach()

string(REGEX MATCHALL
    "\\$\\{GDPP_NATIVE_CMAKE_CONTRACT_ARGS\\}"
    GDPP_TEST_NATIVE_CONTRACT_CONSUMERS
    "${GDPP_TEST_PLUGIN_CMAKE}")
list(LENGTH GDPP_TEST_NATIVE_CONTRACT_CONSUMERS GDPP_TEST_NATIVE_CONTRACT_CONSUMER_COUNT)
if(NOT GDPP_TEST_NATIVE_CONTRACT_CONSUMER_COUNT EQUAL 2)
    message(FATAL_ERROR
        "Expected SDK binding and vendor fixture to share the native CMake contract")
endif()

foreach(GDPP_TEST_GENERATOR_FIELD IN ITEMS CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET)
    string(REGEX MATCHALL
        "${GDPP_TEST_GENERATOR_FIELD}"
        GDPP_TEST_GENERATOR_FIELD_MATCHES
        "${GDPP_TEST_PLUGIN_CMAKE}")
    list(LENGTH GDPP_TEST_GENERATOR_FIELD_MATCHES GDPP_TEST_GENERATOR_FIELD_COUNT)
    if(GDPP_TEST_GENERATOR_FIELD_COUNT LESS 2)
        message(FATAL_ERROR
            "Nested native builds do not preserve ${GDPP_TEST_GENERATOR_FIELD}")
    endif()
endforeach()
