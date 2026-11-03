#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mquickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef JSValue (*mjs_native_fn_t)(JSContext *ctx, JSValue *this_val, int argc,
                                   JSValue *argv, void *opaque);
typedef void (*mjs_event_fn_t)(void *arg);

/* Create the JS context and start the runtime services (timers, event queue,
 * SPIFFS mount).  heap_size is the MQuickJS heap in bytes. */
esp_err_t mjs_engine_init(size_t heap_size);

/* The embedded fallback script (usually main/js/main.js). */
void mjs_engine_set_embedded_script(const uint8_t *data, size_t len);

/* Skip /spiffs/user.js for this boot (BOOT button recovery). */
void mjs_engine_set_user_script_enabled(bool enabled);

/* Run the bootstrap + /spiffs/main.js and then the event loop forever. */
void mjs_engine_run(void) __attribute__((noreturn));

JSContext *mjs_engine_ctx(void);

/* Queue a C callback to run in the JS task.  The callback owns arg and must
 * free it if necessary.  Safe to call from other FreeRTOS tasks. */
void mjs_engine_post(mjs_event_fn_t fn, void *arg);

/* ---- native function registration ------------------------------------- */

/* Create globalThis.<name> and register it for bootstrap globals. */
void mjs_module_begin(JSContext *ctx, const char *name);

void mjs_module_add_fn(JSContext *ctx, const char *module, const char *name,
                       mjs_native_fn_t fn, void *opaque);
void mjs_module_add_int(JSContext *ctx, const char *module, const char *name,
                        int32_t value);
void mjs_module_add_str(JSContext *ctx, const char *module, const char *name,
                        const char *value);
void mjs_module_add_bool(JSContext *ctx, const char *module, const char *name,
                         bool value);

/* ---- helpers shared by modules ---------------------------------------- */

void mjs_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void mjs_dump_exception(JSContext *ctx);

/* Convert a JS string / array / typed array into a freshly allocated byte
 * buffer.  The caller frees it.  Returns NULL on error and sets *len = 0. */
uint8_t *mjs_value_to_bytes(JSContext *ctx, JSValue val, size_t *len);

/* Convert bytes into a JS Array of numbers (portable across MQuickJS). */
JSValue mjs_new_byte_array(JSContext *ctx, const uint8_t *data, size_t len);

/* Read a whole file into a malloc'ed buffer (NUL terminated). */
uint8_t *mjs_read_file(const char *path, size_t *len);
bool mjs_write_file(const char *path, const void *data, size_t len);

/* Call a JS function with up to 4 arguments; dumps and clears exceptions. */
JSValue mjs_call(JSContext *ctx, JSValue func, int argc, const JSValue *argv);

/* Get globalThis[module], or JS_UNDEFINED. */
JSValue mjs_get_module(JSContext *ctx, const char *module);

/* Throw a formatted internal error. */
JSValue mjs_throw(JSContext *ctx, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif
