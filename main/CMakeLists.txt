idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "boards"
    REQUIRES
        mqjs_engine
        mjs_module_system
        mjs_module_fs
        mjs_driver_gpio
        mjs_driver_i2c
        mjs_driver_display
        mjs_module_audio
        mjs_module_wifi
        mjs_module_net
        mjs_module_ble
        mjs_module_web
        mjs_module_mqtt
        mjs_module_ws
        nvs_flash
)

# Board code (board.c, setup_device.c) is deliberately not listed here: the
# generated board component compiles the selected board directory, so board
# sources never enter a shared component.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/js/main.js" TEXT)
