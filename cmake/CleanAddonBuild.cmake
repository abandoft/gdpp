cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED GDPP_ADDON_DIRECTORY OR GDPP_ADDON_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "GDPP_ADDON_DIRECTORY is required")
endif()

cmake_path(ABSOLUTE_PATH GDPP_ADDON_DIRECTORY NORMALIZE OUTPUT_VARIABLE addon_directory)
if(NOT addon_directory MATCHES "[/\\]addons[/\\]gdpp$")
    message(FATAL_ERROR "Refusing to clean unexpected directory: ${addon_directory}")
endif()

file(REMOVE_RECURSE "${addon_directory}/build")
file(GLOB project_products
    "${addon_directory}/binary/libgdpp_project.*"
    "${addon_directory}/binary/gdpp_project.*"
)
if(project_products)
    file(REMOVE ${project_products})
endif()

# Godot persists dynamically loaded development descriptors in the project-local extension
# registry. Leaving one behind after deleting its content-addressed library can crash Godot before
# the editor plugin gets a chance to recover. Preserve customer/third-party entries, but remove
# every descriptor owned by GDPP's generated build workspace.
cmake_path(GET addon_directory PARENT_PATH addons_directory)
cmake_path(GET addons_directory PARENT_PATH project_directory)
set(extension_registry "${project_directory}/.godot/extension_list.cfg")
if(EXISTS "${extension_registry}")
    file(STRINGS "${extension_registry}" extension_entries)
    set(retained_entries)
    foreach(extension_entry IN LISTS extension_entries)
        string(STRIP "${extension_entry}" extension_entry)
        if(extension_entry STREQUAL "" OR
           extension_entry MATCHES "^res://addons/gdpp/build(/|$)")
            continue()
        endif()
        list(APPEND retained_entries "${extension_entry}")
    endforeach()
    if(NOT "res://addons/gdpp/gdpp.gdextension" IN_LIST retained_entries)
        list(APPEND retained_entries "res://addons/gdpp/gdpp.gdextension")
    endif()
    list(JOIN retained_entries "\n" retained_registry)
    file(WRITE "${extension_registry}" "${retained_registry}\n")
endif()
