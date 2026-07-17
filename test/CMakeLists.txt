add_executable(
    gdpp_tests
    main.cpp
    lexer_test.cpp
    parser_test.cpp
    ir_test.cpp
    mir_test.cpp
    godot_api_test.cpp
    native_builder_test.cpp
    project_compiler_test.cpp
    compiler_test.cpp
)
target_link_libraries(gdpp_tests PRIVATE gdpp::core)
target_include_directories(gdpp_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(
    gdpp_tests
    PRIVATE
        GDPP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        GDPP_TEST_BINARY_DIR="${PROJECT_BINARY_DIR}"
)
gdpp_set_project_warnings(gdpp_tests)

add_test(NAME gdpp.unit COMMAND gdpp_tests)

add_test(
    NAME gdpp.architecture
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py"
)

add_test(
    NAME gdpp.addon.clean
    COMMAND "${CMAKE_COMMAND}"
            -DGDPP_TEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DGDPP_TEST_BINARY_DIR=${PROJECT_BINARY_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/clean_addon_build_test.cmake"
)

add_test(
    NAME gdpp.addon.descriptor
    COMMAND "${CMAKE_COMMAND}"
            -DGDPP_TEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/addon_descriptor_test.cmake"
)

if(GDPP_BUILD_CLI)
    add_test(
        NAME gdpp.cli.compile
        COMMAND gdpp compile "${PROJECT_SOURCE_DIR}/example/hello.gd"
                --output "${PROJECT_BINARY_DIR}/test-output"
    )
    set_tests_properties(
        gdpp.cli.compile
        PROPERTIES
            FIXTURES_SETUP generated_example
    )
    add_test(
        NAME gdpp.cli.no_optimize
        COMMAND gdpp compile "${PROJECT_SOURCE_DIR}/example/hello.gd"
                --output "${PROJECT_BINARY_DIR}/test-output-unoptimized" --no-optimize
    )
    add_test(
        NAME gdpp.cli.compile_4_5
        COMMAND gdpp compile "${PROJECT_SOURCE_DIR}/example/hello.gd"
                --output "${PROJECT_BINARY_DIR}/test-output-4.5" --target-godot 4.5
    )
    add_test(
        NAME gdpp.cli.compile_4_7
        COMMAND gdpp compile "${PROJECT_SOURCE_DIR}/example/hello.gd"
                --output "${PROJECT_BINARY_DIR}/test-output-4.7" --target-godot 4.7
    )

    if(GDPP_ENABLE_OFFICIAL_CORPUS_TESTS)
        set(GDPP_CORPUS_MANIFEST
            "${PROJECT_SOURCE_DIR}/test/compatibility/official_projects.json")
        set(GDPP_CORPUS_DIRECTORY
            "${PROJECT_BINARY_DIR}/compatibility/godot-demo-projects")
        set(GDPP_CORPUS_OUTPUT "${PROJECT_BINARY_DIR}/compatibility/results")
        add_test(
            NAME gdpp.compatibility.fetch_official
            COMMAND
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/fetch_compatibility_corpus.py"
                --manifest "${GDPP_CORPUS_MANIFEST}"
                --destination "${GDPP_CORPUS_DIRECTORY}"
                --build-root "${PROJECT_SOURCE_DIR}/build"
        )
        set_tests_properties(
            gdpp.compatibility.fetch_official
            PROPERTIES FIXTURES_SETUP official_compatibility_corpus TIMEOUT 180
        )
        add_test(
            NAME gdpp.compatibility.official_projects
            COMMAND
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/run_compatibility_corpus.py"
                --manifest "${GDPP_CORPUS_MANIFEST}"
                --corpus "${GDPP_CORPUS_DIRECTORY}"
                --compiler "$<TARGET_FILE:gdpp>"
                --sdk-root "${PROJECT_SOURCE_DIR}"
                --output "${GDPP_CORPUS_OUTPUT}"
                --target-godot 4.7
        )
        set_tests_properties(
            gdpp.compatibility.official_projects
            PROPERTIES FIXTURES_REQUIRED official_compatibility_corpus TIMEOUT 600
        )
    endif()
endif()
