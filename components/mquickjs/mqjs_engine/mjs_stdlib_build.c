/*
 * Custom MQuickJS standard library definition.
 *
 * We reuse the upstream mqjs_stdlib.c verbatim and only replace its
 * c_function_decl[] table so that one generic native dispatcher is added to
 * the ROM table.  Every JS-facing module function is then a tiny closure of
 * that dispatcher.  The upstream mquickjs/ tree is not modified.
 */
#define main mqjs_stdlib_original_main
#define js_c_function_decl js_base_c_function_decl
#include "mqjs_stdlib.c"
#undef main
#undef js_c_function_decl

static const JSPropDef js_c_function_decl[] = {
    /* index 0: used internally by Function.prototype.bind */
    { JS_DEF_CFUNC, "bound", { .func = { 0, "0", "generic_params", "js_function_bound" } } },
    /* index 1 (JS_CFUNCTION_USER): generic module dispatcher */
    { JS_DEF_CFUNC, "native", { .func = { 0, "0", "generic_params", "mjs_native_dispatch" } } },
    JS_PROP_END,
};

int main(int argc, char **argv)
{
    return build_atoms("js_stdlib", js_global_object, js_c_function_decl, argc, argv);
}
