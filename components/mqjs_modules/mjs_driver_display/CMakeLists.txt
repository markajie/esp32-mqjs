idf_component_register(
    SRCS
        "mjs_driver_display.c"
        "display_canvas.c"
        "display_panel.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        mqjs_engine
        esp_board_manager
        esp_lcd
        esp_driver_ledc
        esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
