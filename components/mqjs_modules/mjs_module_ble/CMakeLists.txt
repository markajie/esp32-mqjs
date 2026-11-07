idf_component_register(
    SRCS "mjs_module_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES mqjs_engine bt
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
