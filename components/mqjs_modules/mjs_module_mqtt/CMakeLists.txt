idf_component_register(
    SRCS "mjs_module_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES mqjs_engine mjs_module_wifi espressif__mqtt esp-tls
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
