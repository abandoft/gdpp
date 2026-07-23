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
