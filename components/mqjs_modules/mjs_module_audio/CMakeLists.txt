idf_component_register(
    SRCS "mjs_module_audio.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../../main/boards"
    REQUIRES mqjs_engine mjs_driver_i2c esp_driver_i2s esp_driver_gpio espressif__esp_audio_codec
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
