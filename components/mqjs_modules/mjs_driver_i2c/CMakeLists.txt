idf_component_register(
    SRCS "mjs_driver_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES mqjs_engine esp_driver_i2c
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
