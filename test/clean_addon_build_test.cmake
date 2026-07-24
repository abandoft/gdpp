set(fixture "${GDPP_TEST_BINARY_DIR}/test-fixtures/clean-addon/addons/gdpp")
file(REMOVE_RECURSE "${GDPP_TEST_BINARY_DIR}/test-fixtures/clean-addon")
file(MAKE_DIRECTORY "${fixture}/build/project" "${fixture}/binary")
file(WRITE "${fixture}/build/project/manifest.txt" "generated\n")
file(WRITE "${fixture}/binary/libgdpp_compiler.test.so" "compiler\n")
file(WRITE "${fixture}/binary/libgdpp_fallback.test.so" "fallback\n")
file(WRITE "${fixture}/binary/libgdpp.release.test.so" "project\n")
file(WRITE "${fixture}/binary/gdpp.debug.test.dll" "project\n")
file(WRITE "${fixture}/binary/libgdpp_project.release.test.so" "legacy project\n")
file(MAKE_DIRECTORY
    "${fixture}/binary/libgdpp.release.ios.arm64.xcframework"
    "${fixture}/binary/libgdpp_project.release.ios.arm64.xcframework")
file(WRITE
    "${fixture}/binary/libgdpp.release.ios.arm64.xcframework/Info.plist"
    "project\n")
file(WRITE
    "${fixture}/binary/libgdpp_project.release.ios.arm64.xcframework/Info.plist"
    "legacy project\n")

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
foreach(project_product IN ITEMS
        "libgdpp.release.test.so"
        "gdpp.debug.test.dll"
        "libgdpp_project.release.test.so"
        "libgdpp.release.ios.arm64.xcframework"
        "libgdpp_project.release.ios.arm64.xcframework")
    if(EXISTS "${fixture}/binary/${project_product}")
        message(FATAL_ERROR "project native product was not removed: ${project_product}")
    endif()
endforeach()
if(NOT EXISTS "${fixture}/binary/libgdpp_compiler.test.so" OR
   NOT EXISTS "${fixture}/binary/libgdpp_fallback.test.so")
    message(FATAL_ERROR "compiler or fallback library was removed")
endif()
