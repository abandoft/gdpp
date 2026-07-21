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
            "str(class_db_patch)")
        string(FIND "${GDPP_TEST_PACKAGER_SOURCE}" "${GDPP_TEST_CONTRACT}" GDPP_TEST_OFFSET)
        if(GDPP_TEST_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "${GDPP_TEST_PACKAGER} does not preserve the ClassDB contract: "
                "${GDPP_TEST_CONTRACT}")
        endif()
    endforeach()
endforeach()

# Both independent CMake consumers of the packaged Windows SDK must inherit the parent's static
# MSVC CRT contract: the godot-cpp profile builders and the release vendor GDExtension fixture.
file(READ "${GDPP_TEST_SOURCE_DIR}/cmake/GodotPlugin.cmake" GDPP_TEST_PLUGIN_CMAKE)
string(REGEX MATCHALL
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=\\$\\{CMAKE_MSVC_RUNTIME_LIBRARY\\}"
    GDPP_TEST_MSVC_RUNTIME_PROPAGATIONS
    "${GDPP_TEST_PLUGIN_CMAKE}")
list(LENGTH GDPP_TEST_MSVC_RUNTIME_PROPAGATIONS GDPP_TEST_MSVC_RUNTIME_PROPAGATION_COUNT)
if(GDPP_TEST_MSVC_RUNTIME_PROPAGATION_COUNT LESS 2)
    message(FATAL_ERROR
        "Independent target SDK consumers do not preserve the MSVC CRT contract")
endif()
