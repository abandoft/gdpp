# GDPP supplements godot-cpp's public registration API with the one missing
# combination required by GDScript: abstract classes that are runtime-only in
# the editor. The implementation delegates to godot-cpp's existing private
# registration template and does not alter its runtime ABI.
foreach(GDPP_REQUIRED_VARIABLE IN ITEMS GDPP_CLASS_DB_INPUT GDPP_CLASS_DB_OUTPUT)
    if(NOT DEFINED ${GDPP_REQUIRED_VARIABLE})
        message(FATAL_ERROR
            "Missing ${GDPP_REQUIRED_VARIABLE} for the godot-cpp registration patch")
    endif()
endforeach()
if(NOT EXISTS "${GDPP_CLASS_DB_INPUT}")
    message(FATAL_ERROR "godot-cpp ClassDB header is missing: ${GDPP_CLASS_DB_INPUT}")
endif()

file(READ "${GDPP_CLASS_DB_INPUT}" GDPP_CLASS_DB_HEADER)
if(NOT GDPP_CLASS_DB_HEADER MATCHES "register_runtime_abstract_class")
    string(ASCII 9 GDPP_TAB)
    set(GDPP_RUNTIME_DECLARATION
        "${GDPP_TAB}template <typename T>\n${GDPP_TAB}static void register_runtime_class();")
    set(GDPP_RUNTIME_DECLARATION_PATCHED
        "${GDPP_RUNTIME_DECLARATION}\n${GDPP_TAB}template <typename T>\n${GDPP_TAB}static void register_runtime_abstract_class();")
    string(FIND "${GDPP_CLASS_DB_HEADER}" "${GDPP_RUNTIME_DECLARATION}"
        GDPP_RUNTIME_DECLARATION_OFFSET)
    if(GDPP_RUNTIME_DECLARATION_OFFSET LESS 0)
        message(FATAL_ERROR "Unsupported godot-cpp ClassDB declaration layout")
    endif()
    string(REPLACE "${GDPP_RUNTIME_DECLARATION}" "${GDPP_RUNTIME_DECLARATION_PATCHED}"
        GDPP_CLASS_DB_HEADER "${GDPP_CLASS_DB_HEADER}")

    set(GDPP_RUNTIME_DEFINITION
        "template <typename T>\nvoid ClassDB::register_runtime_class() {\n${GDPP_TAB}ClassDB::_register_class<T, false>(false, true, true);\n}")
    set(GDPP_RUNTIME_DEFINITION_PATCHED
        "${GDPP_RUNTIME_DEFINITION}\n\ntemplate <typename T>\nvoid ClassDB::register_runtime_abstract_class() {\n${GDPP_TAB}ClassDB::_register_class<T, true>(false, true, true);\n}")
    string(FIND "${GDPP_CLASS_DB_HEADER}" "${GDPP_RUNTIME_DEFINITION}"
        GDPP_RUNTIME_DEFINITION_OFFSET)
    if(GDPP_RUNTIME_DEFINITION_OFFSET LESS 0)
        message(FATAL_ERROR "Unsupported godot-cpp ClassDB definition layout")
    endif()
    string(REPLACE "${GDPP_RUNTIME_DEFINITION}" "${GDPP_RUNTIME_DEFINITION_PATCHED}"
        GDPP_CLASS_DB_HEADER "${GDPP_CLASS_DB_HEADER}")
endif()

get_filename_component(GDPP_CLASS_DB_OUTPUT_DIRECTORY "${GDPP_CLASS_DB_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${GDPP_CLASS_DB_OUTPUT_DIRECTORY}")
file(WRITE "${GDPP_CLASS_DB_OUTPUT}" "${GDPP_CLASS_DB_HEADER}")
