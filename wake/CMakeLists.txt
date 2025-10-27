cmake_minimum_required(VERSION 3.16)

# Add your local 'components' folder to search path
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(wake)
