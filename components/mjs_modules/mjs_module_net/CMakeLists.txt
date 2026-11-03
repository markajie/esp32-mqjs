idf_component_register(
    SRCS "mjs_module_net.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_engine esp_http_client esp-tls mbedtls lwip
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
