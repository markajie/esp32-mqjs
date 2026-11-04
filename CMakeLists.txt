cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/mjs_modules")

# Apply the project defaults, the tracked board defaults, and (when present)
# the board-manager generated defaults.  The generated component is ignored by
# git, so flash/partition/PSRAM settings must not depend on it being present.
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/main/boards/movecall/moji2-esp32c5/sdkconfig.defaults.board")
set(_board_manager_defaults
    "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
if(EXISTS "${_board_manager_defaults}")
    list(APPEND SDKCONFIG_DEFAULTS "${_board_manager_defaults}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp32_mqjs)

