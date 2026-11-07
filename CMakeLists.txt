cmake_minimum_required(VERSION 3.16)

set(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(BOARDS_DIR "${PROJECT_DIR}/main/boards")

# Components live in two groups.  ESP-IDF names a component after its
# directory, so the nested engine/header directories are listed explicitly.
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_DIR}/components/mqjs_modules"
    "${PROJECT_DIR}/components/mquickjs/mqjs_engine"
    "${PROJECT_DIR}/components/mquickjs/mqjs_headers")

# ---------------------------------------------------------------------------
# Board discovery
#
# A board is any main/boards/<vendor>/<board>/ directory that contains a
# board_info.yaml.  Adding a board therefore never edits a shared file: the
# board directory holds its pins, its drivers and its sdkconfig defaults.
# ---------------------------------------------------------------------------
set(_boards "")
file(GLOB _vendor_dirs LIST_DIRECTORIES true "${BOARDS_DIR}/*")
foreach(_vendor_dir IN LISTS _vendor_dirs)
    file(GLOB _board_dirs LIST_DIRECTORIES true "${_vendor_dir}/*")
    foreach(_board_dir IN LISTS _board_dirs)
        if(EXISTS "${_board_dir}/board_info.yaml")
            get_filename_component(_vendor "${_vendor_dir}" NAME)
            get_filename_component(_board "${_board_dir}" NAME)
            list(APPEND _boards "${_vendor}/${_board}")
        endif()
    endforeach()
endforeach()
list(SORT _boards)

set(BOARD "" CACHE STRING "Board to build as <vendor>/<board>; use 'list' to show the boards")
if(BOARD STREQUAL "" AND DEFINED ENV{BOARD})
    set(BOARD "$ENV{BOARD}")
endif()

if(BOARD STREQUAL "list")
    string(REPLACE ";" "\n  " _board_list "${_boards}")
    message(FATAL_ERROR "Available boards:\n  ${_board_list}\n\nSelect one with -DBOARD=<vendor>/<board>.")
endif()

if(BOARD STREQUAL "")
    list(LENGTH _boards _board_count)
    if(_board_count EQUAL 1)
        list(GET _boards 0 BOARD)
    else()
        string(REPLACE ";" "\n  " _board_list "${_boards}")
        message(FATAL_ERROR "No board selected. Available boards:\n  ${_board_list}\n\nSelect one with -DBOARD=<vendor>/<board>.")
    endif()
endif()

list(FIND _boards "${BOARD}" _board_index)
if(_board_index EQUAL -1)
    string(REPLACE ";" "\n  " _board_list "${_boards}")
    message(FATAL_ERROR "Unknown board '${BOARD}'. Available boards:\n  ${_board_list}")
endif()

set(BOARD_DIR "${BOARDS_DIR}/${BOARD}")
string(REPLACE "/" "_" BOARD_SLUG "${BOARD}")
message(STATUS "Board: ${BOARD}")

# Board identity for logs: BOARD_ID="<vendor>/<board>".
add_compile_definitions(BOARD_ID="${BOARD}")

file(READ "${BOARD_DIR}/board_info.yaml" _board_info)
string(REGEX MATCH "board:[ \t]*([A-Za-z0-9_.-]+)" _board_name_match "${_board_info}")
if(NOT _board_name_match)
    message(FATAL_ERROR "${BOARD_DIR}/board_info.yaml has no 'board:' entry")
endif()
set(BOARD_NAME "${CMAKE_MATCH_1}")

# ---------------------------------------------------------------------------
# Board support component (esp_board_manager)
#
# The board's YAML files describe the devices esp_board_manager owns (the
# panel and its backlight).  Its generator writes components/gen_bmgr_codes,
# and that component also compiles the board directory's .c files, so board
# code never enters a shared component.  Generation is skipped while the
# generated defaults already belong to this board and the description is
# unchanged.
# ---------------------------------------------------------------------------
set(_bmgr_script "${PROJECT_DIR}/managed_components/espressif__esp_board_manager/gen_bmgr_config_codes.py")
set(_bmgr_dir "${PROJECT_DIR}/components/gen_bmgr_codes")
set(_bmgr_defaults "${_bmgr_dir}/board_manager.defaults")

set(_bmgr_generate TRUE)
if(EXISTS "${_bmgr_defaults}" AND EXISTS "${_bmgr_dir}/CMakeLists.txt")
    file(READ "${_bmgr_defaults}" _bmgr_defaults_text)
    if(_bmgr_defaults_text MATCHES "CONFIG_ESP_BOARD_NAME=\"${BOARD_NAME}\"")
        set(_bmgr_generate FALSE)
        foreach(_input board_info.yaml board_peripherals.yaml board_devices.yaml sdkconfig.defaults.board)
            if(EXISTS "${BOARD_DIR}/${_input}" AND "${BOARD_DIR}/${_input}" IS_NEWER_THAN "${_bmgr_defaults}")
                set(_bmgr_generate TRUE)
            endif()
        endforeach()
    endif()
endif()

if(_bmgr_generate)
    if(EXISTS "${_bmgr_script}")
        if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
            if(WIN32)
                set(_bmgr_python "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python.exe")
            else()
                set(_bmgr_python "$ENV{IDF_PYTHON_ENV_PATH}/bin/python")
            endif()
        endif()
        if(NOT EXISTS "${_bmgr_python}")
            set(_bmgr_python "python")
        endif()
        execute_process(
            COMMAND "${_bmgr_python}" "${_bmgr_script}"
                    -b "main/boards/${BOARD}" --project-dir "${PROJECT_DIR}"
                    --skip-sdkconfig-check
            WORKING_DIRECTORY "${PROJECT_DIR}"
            RESULT_VARIABLE _bmgr_result
            OUTPUT_QUIET
            ERROR_VARIABLE _bmgr_error)
        if(NOT _bmgr_result EQUAL 0)
            message(FATAL_ERROR "Board support generation failed:\n${_bmgr_error}")
        endif()
        message(STATUS "Generated board support: ${BOARD_NAME}")
    else()
        message(STATUS "esp_board_manager is not installed yet; the build will stop after downloading it.")
    endif()
endif()

# ---------------------------------------------------------------------------
# sdkconfig
#
# Every board keeps its own sdkconfig.<vendor>_<board>, so switching boards
# never mixes targets, flash or PSRAM settings.  The target is guessed from
# sdkconfig.defaults.board, so no explicit 'set-target' is needed.
# ---------------------------------------------------------------------------
set(SDKCONFIG "${PROJECT_DIR}/sdkconfig.${BOARD_SLUG}")
set(SDKCONFIG_DEFAULTS "${PROJECT_DIR}/sdkconfig.defaults")
if(EXISTS "${BOARD_DIR}/sdkconfig.defaults.board")
    list(APPEND SDKCONFIG_DEFAULTS "${BOARD_DIR}/sdkconfig.defaults.board")
endif()
if(EXISTS "${_bmgr_defaults}")
    list(APPEND SDKCONFIG_DEFAULTS "${_bmgr_defaults}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp32_mqjs)

set(_bmgr_ready FALSE)
if(EXISTS "${_bmgr_defaults}")
    file(READ "${_bmgr_defaults}" _bmgr_defaults_text)
    if(_bmgr_defaults_text MATCHES "CONFIG_ESP_BOARD_NAME=\"${BOARD_NAME}\"")
        set(_bmgr_ready TRUE)
    endif()
endif()
if(NOT _bmgr_ready)
    message(FATAL_ERROR
        "Board support for '${BOARD}' is not generated yet.  The managed components "
        "were just installed; run the build again to generate it.")
endif()
