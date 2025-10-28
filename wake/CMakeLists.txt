cmake_minimum_required(VERSION 3.16)

# Make sure local components and ADF components are visible to the build
list(APPEND EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components
    ${CMAKE_CURRENT_LIST_DIR}/components/esp-adf/components
    #${CMAKE_CURRENT_LIST_DIR}/components/esp_sr
)
set(PARTITION_TABLE_FILENAME "partitions.csv")

# Compute ADF version (optional metadata) and export as a compile define project-wide.
# We run this here at project-level so we can safely call add_compile_definitions().
execute_process(
    COMMAND git describe --always --tags --dirty
    OUTPUT_VARIABLE ADF_VER_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/components/esp-adf
    RESULT_VARIABLE _git_res
)

if(NOT _git_res EQUAL 0)
    set(ADF_VER_RAW "unknown")
endif()

# Make a sanitized macro value (escape quotes)
string(REPLACE "\"" "\\\"" ADF_VER_ESCAPED "${ADF_VER_RAW}")

# Add compile definition globally (project-level; allowed)
add_compile_definitions(ADF_VER="${ADF_VER_ESCAPED}")

# Now include the IDF project machinery
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(wake)
