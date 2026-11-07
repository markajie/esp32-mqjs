idf_component_register(
    SRCS "mjs_module_web.c"
    INCLUDE_DIRS "include"
    REQUIRES mqjs_engine esp_http_server
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
