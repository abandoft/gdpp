if(NOT DEFINED GDPP_TEST_BINARY_DIR OR NOT DEFINED GDPP_TEST_SDK_VERSIONS)
    message(FATAL_ERROR "SDK profile contract test requires the build directory and SDK versions")
endif()

foreach(GDPP_TEST_SDK_VERSION IN LISTS GDPP_TEST_SDK_VERSIONS)
    string(REPLACE "." "_" GDPP_TEST_SDK_SUFFIX "${GDPP_TEST_SDK_VERSION}")
    set(GDPP_TEST_CACHE
        "${GDPP_TEST_BINARY_DIR}/sdk/gdpp_godot_cpp_${GDPP_TEST_SDK_SUFFIX}_release/CMakeCache.txt")
    if(NOT EXISTS "${GDPP_TEST_CACHE}")
        message(FATAL_ERROR "Packaged Release SDK cache is missing: ${GDPP_TEST_CACHE}")
    endif()

    file(STRINGS "${GDPP_TEST_CACHE}" GDPP_TEST_BUILD_TYPE_LINE
        REGEX "^CMAKE_BUILD_TYPE:STRING=")
    if(NOT GDPP_TEST_BUILD_TYPE_LINE STREQUAL "CMAKE_BUILD_TYPE:STRING=Release")
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} distribution binding must use Release, found "
            "'${GDPP_TEST_BUILD_TYPE_LINE}'")
    endif()
endforeach()
