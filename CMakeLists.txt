cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/mjs_modules")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp32_mqjs)

