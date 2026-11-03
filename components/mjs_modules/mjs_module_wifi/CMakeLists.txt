idf_component_register(
    SRCS "mjs_module_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_engine esp_wifi esp_netif esp_event nvs_flash
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
