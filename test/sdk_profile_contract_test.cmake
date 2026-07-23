if(NOT DEFINED GDPP_TEST_BINARY_DIR OR NOT DEFINED GDPP_TEST_SDK_VERSIONS OR
        NOT DEFINED GDPP_TEST_ADDON_DIR OR NOT DEFINED GDPP_TEST_CONFIG)
    message(FATAL_ERROR "SDK profile contract test requires the build directory and SDK versions")
endif()

if(GDPP_TEST_CONFIG STREQUAL "")
    set(GDPP_TEST_CONFIG Debug)
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
    file(STRINGS "${GDPP_TEST_CACHE}" GDPP_TEST_CONFIGURATION_TYPES_LINE
        REGEX "^CMAKE_CONFIGURATION_TYPES:STRING=")
    string(REPLACE "\\;" ";" GDPP_TEST_CONFIGURATION_TYPES
        "${GDPP_TEST_CONFIGURATION_TYPES_LINE}")
    if(NOT GDPP_TEST_BUILD_TYPE_LINE STREQUAL "CMAKE_BUILD_TYPE:STRING=Release" AND
            NOT GDPP_TEST_CONFIGURATION_TYPES MATCHES "(^|;)Release(;|$)")
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} distribution binding cannot produce Release; "
            "single-config='${GDPP_TEST_BUILD_TYPE_LINE}', "
            "multi-config='${GDPP_TEST_CONFIGURATION_TYPES}'")
    endif()

    set(GDPP_TEST_SDK_ROOT "${GDPP_TEST_ADDON_DIR}/sdk/${GDPP_TEST_SDK_VERSION}")
    file(GLOB GDPP_TEST_BINDINGS LIST_DIRECTORIES false "${GDPP_TEST_SDK_ROOT}/lib/*")
    list(LENGTH GDPP_TEST_BINDINGS GDPP_TEST_BINDING_COUNT)
    if(NOT GDPP_TEST_BINDING_COUNT EQUAL 2)
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} host SDK must contain exactly editor and "
            "template_release bindings, found: ${GDPP_TEST_BINDINGS}")
    endif()
    set(GDPP_TEST_EDITOR_BINDINGS ${GDPP_TEST_BINDINGS})
    list(FILTER GDPP_TEST_EDITOR_BINDINGS INCLUDE REGEX "\\.editor\\.")
    list(LENGTH GDPP_TEST_EDITOR_BINDINGS GDPP_TEST_EDITOR_BINDING_COUNT)
    if(NOT GDPP_TEST_EDITOR_BINDING_COUNT EQUAL 1)
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} host SDK must contain exactly one editor binding")
    endif()
    set(GDPP_TEST_RELEASE_BINDINGS ${GDPP_TEST_BINDINGS})
    list(FILTER GDPP_TEST_RELEASE_BINDINGS INCLUDE REGEX "\\.template_release\\.")
    list(LENGTH GDPP_TEST_RELEASE_BINDINGS GDPP_TEST_RELEASE_BINDING_COUNT)
    if(NOT GDPP_TEST_RELEASE_BINDING_COUNT EQUAL 1)
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} host SDK must contain exactly one template_release "
            "binding")
    endif()

    file(STRINGS "${GDPP_TEST_SDK_ROOT}/sdk.manifest" GDPP_TEST_DISTRIBUTION_BINDING_LINE
        REGEX "^distribution_binding ")
    if(NOT GDPP_TEST_DISTRIBUTION_BINDING_LINE STREQUAL
            "distribution_binding template_release")
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} SDK has an invalid distribution binding contract")
    endif()
    file(STRINGS "${GDPP_TEST_SDK_ROOT}/sdk.manifest" GDPP_TEST_EDITOR_OPTIMIZATION_LINE
        REGEX "^editor_optimization ")
    if(NOT GDPP_TEST_EDITOR_OPTIMIZATION_LINE STREQUAL
            "editor_optimization ${GDPP_TEST_CONFIG}")
        message(FATAL_ERROR
            "Godot ${GDPP_TEST_SDK_VERSION} editor optimization does not match its parent build")
    endif()
endforeach()
