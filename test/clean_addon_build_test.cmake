set(fixture "${GDPP_TEST_BINARY_DIR}/test-fixtures/clean-addon/addons/gdpp")
file(REMOVE_RECURSE "${GDPP_TEST_BINARY_DIR}/test-fixtures/clean-addon")
file(MAKE_DIRECTORY "${fixture}/build/project" "${fixture}/binary")
file(WRITE "${fixture}/build/project/manifest.txt" "generated\n")
file(WRITE "${fixture}/binary/libgdpp_compiler.test.so" "compiler\n")
file(WRITE "${fixture}/binary/libgdpp_fallback.test.so" "fallback\n")
file(WRITE "${fixture}/binary/libgdpp_project.release.test.so" "project\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -DGDPP_ADDON_DIRECTORY=${fixture}
            -P "${GDPP_TEST_SOURCE_DIR}/cmake/CleanAddonBuild.cmake"
    RESULT_VARIABLE clean_result
)
if(NOT clean_result EQUAL 0)
    message(FATAL_ERROR "add-on product cleanup failed")
endif()
if(EXISTS "${fixture}/build")
    message(FATAL_ERROR "add-on build workspace was not removed")
endif()
if(EXISTS "${fixture}/binary/libgdpp_project.release.test.so")
    message(FATAL_ERROR "project native library was not removed")
endif()
if(NOT EXISTS "${fixture}/binary/libgdpp_compiler.test.so" OR
   NOT EXISTS "${fixture}/binary/libgdpp_fallback.test.so")
    message(FATAL_ERROR "compiler or fallback library was removed")
endif()
