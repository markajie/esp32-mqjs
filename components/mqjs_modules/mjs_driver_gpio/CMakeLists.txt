idf_component_register(
    SRCS "mjs_driver_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES mqjs_engine esp_driver_gpio
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
