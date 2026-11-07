idf_component_register(
    SRCS
        "./mquickjs/mquickjs.c"
        "./mquickjs/cutils.c"
        "./mquickjs/dtoa.c"
        "./mquickjs/libm.c"
    INCLUDE_DIRS
        "./mquickjs"
    PRIV_INCLUDE_DIRS
        "./mquickjs"
    PRIV_REQUIRES
        mqjs_headers
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Os
    -fno-strict-aliasing
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wno-implicit-fallthrough
    -Wno-format
    -Wno-type-limits
    -Wno-dangling-pointer
)
