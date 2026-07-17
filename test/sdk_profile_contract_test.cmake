if(NOT DEFINED GDPP_TEST_BINARY_DIR OR NOT DEFINED GDPP_TEST_SDK_VERSIONS)
    message(FATAL_ERROR "SDK profile contract test requires the build directory and SDK versions")
endif()

foreach(GDPP_TEST_SDK_VERSION IN LISTS GDPP_TEST_SDK_VERSIONS)
    string(REPLACE "." "_" GDPP_TEST_SDK_SUFFIX "${GDPP_TEST_SDK_VERSION}")
    foreach(GDPP_TEST_SDK_PROFILE IN ITEMS debug release)
        if(GDPP_TEST_SDK_PROFILE STREQUAL "debug")
            set(GDPP_TEST_EXPECTED_BUILD_TYPE Debug)
        else()
            set(GDPP_TEST_EXPECTED_BUILD_TYPE Release)
        endif()

        set(GDPP_TEST_CACHE
            "${GDPP_TEST_BINARY_DIR}/sdk/gdpp_godot_cpp_${GDPP_TEST_SDK_SUFFIX}_${GDPP_TEST_SDK_PROFILE}/CMakeCache.txt")
        if(NOT EXISTS "${GDPP_TEST_CACHE}")
            message(FATAL_ERROR "Packaged SDK profile cache is missing: ${GDPP_TEST_CACHE}")
        endif()

        file(STRINGS "${GDPP_TEST_CACHE}" GDPP_TEST_BUILD_TYPE_LINE
            REGEX "^CMAKE_BUILD_TYPE:STRING=")
        if(NOT GDPP_TEST_BUILD_TYPE_LINE STREQUAL
                "CMAKE_BUILD_TYPE:STRING=${GDPP_TEST_EXPECTED_BUILD_TYPE}")
            message(FATAL_ERROR
                "Godot ${GDPP_TEST_SDK_VERSION} ${GDPP_TEST_SDK_PROFILE} binding must use "
                "${GDPP_TEST_EXPECTED_BUILD_TYPE}, found '${GDPP_TEST_BUILD_TYPE_LINE}'")
        endif()
    endforeach()
endforeach()
