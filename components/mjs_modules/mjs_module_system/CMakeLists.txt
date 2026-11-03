idf_component_register(
    SRCS "mjs_module_system.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_engine esp_system esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
