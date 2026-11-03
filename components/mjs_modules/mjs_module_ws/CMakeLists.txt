idf_component_register(
    SRCS "mjs_module_ws.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_engine mjs_module_wifi espressif__esp_websocket_client esp-tls
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
