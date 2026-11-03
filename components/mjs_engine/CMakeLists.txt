idf_component_register(
    SRCS "mjs_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_headers mquickjs spiffs nvs_flash esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
