idf_component_register(
    SRCS "mjs_module_fs.c"
    INCLUDE_DIRS "include"
    REQUIRES mjs_engine spiffs
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wno-unused-parameter)
