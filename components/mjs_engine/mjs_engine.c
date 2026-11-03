/*
 * MQuickJS runtime glue for ESP-IDF.
 *
 * The engine itself lives in the pristine mquickjs/ tree.  This file owns the
 * heap, timers, event queue, filesystem glue and a generic native-function
 * dispatcher registered in the generated ROM stdlib table.
 */
#include "mjs_engine.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* The generated ROM table refers to these symbols. */
static JSValue mjs_native_dispatch(JSContext *ctx, JSValue *this_val, int argc,
                                   JSValue *argv, JSValue params);
static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#include "mqjs_stdlib.h"

#define TAG "mjs"

/* index 1 in the generated c_function_table: see mjs_stdlib_build.c */
#define MJS_NATIVE_FUNC_INDEX 1
#define MJS_MAX_NATIVE 192
#define MJS_MAX_MODULES 24
#define MJS_MAX_TIMERS 32
#define MJS_EVENT_QUEUE_LEN 32
#define MJS_EVAL_TIMEOUT_MS 10000

typedef struct {
    mjs_native_fn_t fn;
    void *opaque;
} native_entry_t;

typedef struct {
    mjs_event_fn_t fn;
    void *arg;
} event_t;

typedef struct {
    bool used;
    JSGCRef func;
    int64_t due_ms;
} mjs_timer_t;

static JSContext *s_ctx;
static void *s_heap;
static size_t s_heap_size;
static const uint8_t *s_embedded;
static size_t s_embedded_len;

static native_entry_t s_native[MJS_MAX_NATIVE];
static int s_native_count;
static char s_modules[MJS_MAX_MODULES][24];
static int s_module_count;
static mjs_timer_t s_timers[MJS_MAX_TIMERS];
static QueueHandle_t s_event_q;
static int64_t s_deadline_us;

static char s_log_line[256];
static size_t s_log_len;

/* ------------------------------------------------------------------------- */
/* logging                                                                   */
/* ------------------------------------------------------------------------- */

static void log_flush(void)
{
    if (s_log_len == 0)
        return;
    s_log_line[s_log_len] = '\0';
    ESP_LOGI(TAG, "%s", s_log_line);
    s_log_len = 0;
}

static void log_write(void *opaque, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    (void)opaque;
    for (size_t i = 0; i < len; i++) {
        char c = p[i];
        if (c == '\n' || c == '\r') {
            log_flush();
        } else {
            if (s_log_len >= sizeof(s_log_line) - 1)
                log_flush();
            s_log_line[s_log_len++] = c;
        }
    }
}

void mjs_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_write(NULL, buf, strlen(buf));
    log_write(NULL, "\n", 1);
}

static int eval_interrupt(JSContext *ctx, void *opaque)
{
    (void)ctx;
    (void)opaque;
    return esp_timer_get_time() > s_deadline_us;
}

static void eval_begin(void)
{
    s_deadline_us = esp_timer_get_time() + (int64_t)MJS_EVAL_TIMEOUT_MS * 1000;
}

void mjs_dump_exception(JSContext *ctx)
{
    JSValue obj = JS_GetException(ctx);
    log_flush();
    JS_PrintValueF(ctx, obj, JS_DUMP_LONG);
    log_write(NULL, "\n", 1);
    log_flush();
}

JSValue mjs_throw(JSContext *ctx, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return JS_ThrowInternalError(ctx, "%s", buf);
}

/* ------------------------------------------------------------------------- */
/* native function dispatch                                                  */
/* ------------------------------------------------------------------------- */

static JSValue mjs_native_dispatch(JSContext *ctx, JSValue *this_val, int argc,
                                   JSValue *argv, JSValue params)
{
    int id = -1;
    if (JS_ToInt32(ctx, &id, params))
        return JS_EXCEPTION;
    if (id < 0 || id >= s_native_count || !s_native[id].fn)
        return JS_ThrowInternalError(ctx, "bad native function id %d", id);
    return s_native[id].fn(ctx, this_val, argc, argv, s_native[id].opaque);
}

static int native_register(mjs_native_fn_t fn, void *opaque)
{
    for (int i = 0; i < s_native_count; i++) {
        if (s_native[i].fn == fn && s_native[i].opaque == opaque)
            return i;
    }
    if (s_native_count >= MJS_MAX_NATIVE)
        return -1;
    s_native[s_native_count].fn = fn;
    s_native[s_native_count].opaque = opaque;
    return s_native_count++;
}

/* ------------------------------------------------------------------------- */
/* JS object helpers                                                         */
/* ------------------------------------------------------------------------- */

static void global_set(JSContext *ctx, const char *name, JSValue val)
{
    JSGCRef g_ref, v_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    JSValue *v = JS_PushGCRef(ctx, &v_ref);
    *g = JS_GetGlobalObject(ctx);
    *v = val;
    JS_SetPropertyStr(ctx, *g, name, *v);
    JS_PopGCRef(ctx, &v_ref);
    JS_PopGCRef(ctx, &g_ref);
}

static bool module_record(const char *name)
{
    for (int i = 0; i < s_module_count; i++) {
        if (!strcmp(s_modules[i], name))
            return false;
    }
    if (s_module_count >= MJS_MAX_MODULES)
        return false;
    snprintf(s_modules[s_module_count], sizeof(s_modules[0]), "%s", name);
    s_module_count++;
    return true;
}

void mjs_module_begin(JSContext *ctx, const char *name)
{
    JSGCRef o_ref;
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *o = JS_NewObject(ctx);
    if (JS_IsException(*o)) {
        JS_PopGCRef(ctx, &o_ref);
        mjs_dump_exception(ctx);
        return;
    }
    global_set(ctx, name, *o);
    JS_PopGCRef(ctx, &o_ref);
    module_record(name);
}

JSValue mjs_get_module(JSContext *ctx, const char *module)
{
    JSGCRef g_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    *g = JS_GetGlobalObject(ctx);
    JSValue obj = JS_GetPropertyStr(ctx, *g, module);
    JS_PopGCRef(ctx, &g_ref);
    return obj;
}

void mjs_module_add_fn(JSContext *ctx, const char *module, const char *name,
                       mjs_native_fn_t fn, void *opaque)
{
    int id = native_register(fn, opaque);
    if (id < 0) {
        mjs_log("native function table full (%s.%s)", module, name);
        return;
    }
    JSGCRef g_ref, o_ref, f_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    JSValue *f = JS_PushGCRef(ctx, &f_ref);
    *g = JS_GetGlobalObject(ctx);
    *o = JS_GetPropertyStr(ctx, *g, module);
    if (JS_IsUndefined(*o)) {
        mjs_log("module %s not found", module);
        JS_PopGCRef(ctx, &f_ref);
        JS_PopGCRef(ctx, &o_ref);
        JS_PopGCRef(ctx, &g_ref);
        return;
    }
    *f = JS_NewCFunctionParams(ctx, MJS_NATIVE_FUNC_INDEX, JS_NewInt32(ctx, id));
    if (!JS_IsException(*f))
        JS_SetPropertyStr(ctx, *o, name, *f);
    JS_PopGCRef(ctx, &f_ref);
    JS_PopGCRef(ctx, &o_ref);
    JS_PopGCRef(ctx, &g_ref);
}

void mjs_module_add_int(JSContext *ctx, const char *module, const char *name,
                        int32_t value)
{
    JSGCRef g_ref, o_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *g = JS_GetGlobalObject(ctx);
    *o = JS_GetPropertyStr(ctx, *g, module);
    JS_SetPropertyStr(ctx, *o, name, JS_NewInt32(ctx, value));
    JS_PopGCRef(ctx, &o_ref);
    JS_PopGCRef(ctx, &g_ref);
}

void mjs_module_add_str(JSContext *ctx, const char *module, const char *name,
                        const char *value)
{
    JSGCRef g_ref, o_ref, v_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    JSValue *v = JS_PushGCRef(ctx, &v_ref);
    *g = JS_GetGlobalObject(ctx);
    *o = JS_GetPropertyStr(ctx, *g, module);
    *v = JS_NewString(ctx, value);
    if (!JS_IsException(*v))
        JS_SetPropertyStr(ctx, *o, name, *v);
    JS_PopGCRef(ctx, &v_ref);
    JS_PopGCRef(ctx, &o_ref);
    JS_PopGCRef(ctx, &g_ref);
}

void mjs_module_add_bool(JSContext *ctx, const char *module, const char *name,
                         bool value)
{
    JSGCRef g_ref, o_ref;
    JSValue *g = JS_PushGCRef(ctx, &g_ref);
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *g = JS_GetGlobalObject(ctx);
    *o = JS_GetPropertyStr(ctx, *g, module);
    JS_SetPropertyStr(ctx, *o, name, JS_NewBool(value));
    JS_PopGCRef(ctx, &o_ref);
    JS_PopGCRef(ctx, &g_ref);
}

JSValue mjs_call(JSContext *ctx, JSValue func, int argc, const JSValue *argv)
{
    JSValue ret;
    eval_begin();
    if (JS_StackCheck(ctx, (uint32_t)argc + 2))
        return JS_EXCEPTION;
    /* The MQuickJS stack stores the last parameter first. */
    for (int i = argc - 1; i >= 0; i--)
        JS_PushArg(ctx, argv[i]);
    JS_PushArg(ctx, func);
    JS_PushArg(ctx, JS_NULL);
    ret = JS_Call(ctx, argc);
    if (JS_IsException(ret))
        mjs_dump_exception(ctx);
    return ret;
}

/* ------------------------------------------------------------------------- */
/* byte conversion                                                           */
/* ------------------------------------------------------------------------- */

uint8_t *mjs_value_to_bytes(JSContext *ctx, JSValue val, size_t *len)
{
    *len = 0;
    if (JS_IsString(ctx, val)) {
        JSCStringBuf sbuf;
        size_t slen = 0;
        const char *s = JS_ToCStringLen(ctx, &slen, val, &sbuf);
        if (!s)
            return NULL;
        uint8_t *out = malloc(slen + 1);
        if (!out)
            return NULL;
        memcpy(out, s, slen);
        out[slen] = 0;
        *len = slen;
        return out;
    }
    if (!JS_IsPtr(val))
        return NULL;

    JSValue lenv = JS_GetPropertyStr(ctx, val, "length");
    int n = 0;
    if (JS_ToInt32(ctx, &n, lenv) || n < 0 || n > (1 << 20))
        return NULL;
    uint8_t *out = malloc((size_t)n + 1);
    if (!out)
        return NULL;
    for (int i = 0; i < n; i++) {
        int v = 0;
        JSValue item = JS_GetPropertyUint32(ctx, val, (uint32_t)i);
        if (JS_ToInt32(ctx, &v, item)) {
            free(out);
            return NULL;
        }
        out[i] = (uint8_t)v;
    }
    out[n] = 0;
    *len = (size_t)n;
    return out;
}

JSValue mjs_new_byte_array(JSContext *ctx, const uint8_t *data, size_t len)
{
    if (len > (1 << 20))
        return JS_ThrowRangeError(ctx, "buffer too large");
    JSGCRef a_ref;
    JSValue *a = JS_PushGCRef(ctx, &a_ref);
    *a = JS_NewArray(ctx, (int)len);
    if (JS_IsException(*a)) {
        JS_PopGCRef(ctx, &a_ref);
        return JS_EXCEPTION;
    }
    for (size_t i = 0; i < len; i++)
        JS_SetPropertyUint32(ctx, *a, (uint32_t)i, JS_NewInt32(ctx, data[i]));
    JSValue ret = *a;
    JS_PopGCRef(ctx, &a_ref);
    return ret;
}

/* ------------------------------------------------------------------------- */
/* file helpers                                                              */
/* ------------------------------------------------------------------------- */

uint8_t *mjs_read_file(const char *path, size_t *len)
{
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > (4 << 20)) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *len = got;
    return buf;
}

bool mjs_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    return wrote == len;
}

/* ------------------------------------------------------------------------- */
/* standard library callbacks                                                */
/* ------------------------------------------------------------------------- */

static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        if (i)
            log_write(NULL, " ", 1);
        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf sbuf;
            size_t len = 0;
            const char *s = JS_ToCStringLen(ctx, &len, argv[i], &sbuf);
            if (s)
                log_write(NULL, s, len);
        } else {
            JS_PrintValueF(ctx, argv[i], JS_DUMP_LONG);
        }
    }
    log_write(NULL, "\n", 1);
    return JS_UNDEFINED;
}

static JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_GC(ctx);
    return JS_UNDEFINED;
}

static int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int64_t get_date_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static JSValue js_date_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val;
    double val;
    argc &= ~FRAME_CF_CTOR;
    if (argc == 0) {
        val = (double)get_date_ms();
    } else if (argc == 1 && JS_IsNumber(ctx, argv[0])) {
        if (JS_ToNumber(ctx, &val, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "unsupported Date() parameter");
    }
    return JS_NewDate(ctx, val);
}

static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, get_date_ms());
}

static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc,
                                  JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, get_time_ms());
}

static JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "load(path)");
    JSCStringBuf sbuf;
    const char *path = JS_ToCString(ctx, argv[0], &sbuf);
    if (!path)
        return JS_EXCEPTION;
    size_t len = 0;
    uint8_t *buf = mjs_read_file(path, &len);
    if (!buf)
        return JS_ThrowInternalError(ctx, "cannot read %s", path);
    eval_begin();
    JSValue ret = JS_Eval(ctx, (const char *)buf, len, path, 0);
    free(buf);
    return ret;
}

/* ---- timers ----------------------------------------------------------- */

static JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc,
                             JSValue *argv)
{
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "setTimeout(fn, ms)");
    int delay = 0;
    if (argc >= 2 && JS_ToInt32(ctx, &delay, argv[1]))
        return JS_EXCEPTION;
    for (int i = 0; i < MJS_MAX_TIMERS; i++) {
        if (s_timers[i].used)
            continue;
        JSValue *p = JS_AddGCRef(ctx, &s_timers[i].func);
        *p = argv[0];
        s_timers[i].used = true;
        s_timers[i].due_ms = get_time_ms() + (delay > 0 ? delay : 0);
        return JS_NewInt32(ctx, i);
    }
    return JS_ThrowInternalError(ctx, "too many timers");
}

static JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc,
                               JSValue *argv)
{
    (void)this_val;
    int id = -1;
    if (argc >= 1 && JS_ToInt32(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (id >= 0 && id < MJS_MAX_TIMERS && s_timers[id].used) {
        JS_DeleteGCRef(ctx, &s_timers[id].func);
        s_timers[id].used = false;
    }
    return JS_UNDEFINED;
}

static void run_timers(void)
{
    int64_t now = get_time_ms();
    for (int i = 0; i < MJS_MAX_TIMERS; i++) {
        mjs_timer_t *t = &s_timers[i];
        if (!t->used || t->due_ms > now)
            continue;
        JSGCRef f_ref;
        JSValue *f = JS_PushGCRef(s_ctx, &f_ref);
        *f = t->func.val;
        JS_DeleteGCRef(s_ctx, &t->func);
        t->used = false;
        if (JS_IsFunction(s_ctx, *f)) {
            eval_begin();
            mjs_call(s_ctx, *f, 0, NULL);
        }
        JS_PopGCRef(s_ctx, &f_ref);
    }
}

/* ------------------------------------------------------------------------- */
/* event queue                                                               */
/* ------------------------------------------------------------------------- */

void mjs_engine_post(mjs_event_fn_t fn, void *arg)
{
    if (!s_event_q || !fn)
        return;
    event_t ev = { .fn = fn, .arg = arg };
    if (xQueueSend(s_event_q, &ev, 0) != pdTRUE)
        mjs_log("event queue full, dropping event");
}

static void process_events(void)
{
    event_t ev;
    while (xQueueReceive(s_event_q, &ev, 0) == pdTRUE) {
        eval_begin();
        ev.fn(ev.arg);
    }
}

/* ------------------------------------------------------------------------- */
/* engine lifecycle                                                          */
/* ------------------------------------------------------------------------- */

esp_err_t mjs_engine_init(size_t heap_size)
{
    if (s_ctx)
        return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK)
        mjs_log("SPIFFS mount failed: %s", esp_err_to_name(err));

    s_event_q = xQueueCreate(MJS_EVENT_QUEUE_LEN, sizeof(event_t));
    if (!s_event_q)
        return ESP_ERR_NO_MEM;

    s_heap = heap_caps_aligned_alloc(16, heap_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_heap)
        s_heap = heap_caps_aligned_alloc(16, heap_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_heap) {
        mjs_log("cannot allocate %u byte JS heap", (unsigned)heap_size);
        return ESP_ERR_NO_MEM;
    }
    s_heap_size = heap_size;

    s_ctx = JS_NewContext(s_heap, s_heap_size, &js_stdlib);
    if (!s_ctx) {
        mjs_log("JS_NewContext failed");
        return ESP_FAIL;
    }
    JS_SetLogFunc(s_ctx, log_write);
    JS_SetRandomSeed(s_ctx, esp_random());
    JS_SetInterruptHandler(s_ctx, eval_interrupt);
    mjs_log("MQuickJS ready, heap=%u bytes", (unsigned)heap_size);
    return ESP_OK;
}

JSContext *mjs_engine_ctx(void)
{
    return s_ctx;
}

void mjs_engine_set_embedded_script(const uint8_t *data, size_t len)
{
    s_embedded = data;
    s_embedded_len = len;
}

static char *build_bootstrap(void)
{
    size_t cap = 512 + (size_t)s_module_count * 48;
    char *s = malloc(cap);
    if (!s)
        return NULL;
    size_t off = 0;
    off += snprintf(s + off, cap - off,
                    "var setTimeout=globalThis.setTimeout;"
                    "var clearTimeout=globalThis.clearTimeout;"
                    "var setInterval=globalThis.setInterval||function(fn,ms){"
                    "function tick(){fn();setTimeout(tick,ms);}"
                    "return setTimeout(tick,ms);};"
                    "var clearInterval=globalThis.clearInterval||clearTimeout;");
    if (off >= cap)
        off = cap - 1;
    for (int i = 0; i < s_module_count; i++) {
        if (off >= cap)
            break;
        off += snprintf(s + off, cap - off, "var %s = globalThis.%s;",
                        s_modules[i], s_modules[i]);
    }
    s[cap - 1] = 0;
    return s;
}

static bool s_user_script_enabled = true;

void mjs_engine_set_user_script_enabled(bool enabled)
{
    s_user_script_enabled = enabled;
}

static void run_script(void)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    const char *filename = "/spiffs/main.js";
    bool from_embedded = false;

    /* A user script can be uploaded from the web UI.  It takes priority over
     * the firmware image; a syntax/runtime error falls back to main.js so the
     * upload page remains reachable. */
    if (s_user_script_enabled)
        buf = mjs_read_file("/spiffs/user.js", &len);
    if (buf && len) {
        mjs_log("running /spiffs/user.js (%u bytes)", (unsigned)len);
        eval_begin();
        JSValue ret = JS_Eval(s_ctx, (const char *)buf, len, "/spiffs/user.js", 0);
        free(buf);
        if (!JS_IsException(ret)) {
            mjs_log("user.js finished");
            return;
        }
        mjs_dump_exception(s_ctx);
        mjs_log("user.js failed; falling back to main.js");
        buf = NULL;
        len = 0;
    } else if (buf) {
        free(buf);
        buf = NULL;
        len = 0;
    }

    buf = mjs_read_file(filename, &len);
    if (s_embedded && s_embedded_len &&
        (!buf || len != s_embedded_len ||
         memcmp(buf, s_embedded, s_embedded_len) != 0)) {
        /* The firmware owns main.js; refresh it when the embedded copy changed. */
        mjs_log("installing embedded main.js (%u bytes)", (unsigned)s_embedded_len);
        if (mjs_write_file(filename, s_embedded, s_embedded_len)) {
            free(buf);
            buf = malloc(s_embedded_len + 1);
            if (buf) {
                memcpy(buf, s_embedded, s_embedded_len);
                buf[s_embedded_len] = 0;
                len = s_embedded_len;
                from_embedded = true;
            }
        } else if (!buf) {
            buf = malloc(s_embedded_len + 1);
            if (buf) {
                memcpy(buf, s_embedded, s_embedded_len);
                buf[s_embedded_len] = 0;
                len = s_embedded_len;
                from_embedded = true;
            }
        }
    }
    if (!buf) {
        mjs_log("no /spiffs/main.js and no embedded script");
        return;
    }
    mjs_log("running %s (%u bytes%s)", filename, (unsigned)len,
            from_embedded ? ", embedded fallback" : "");

    eval_begin();
    JSValue ret = JS_Eval(s_ctx, (const char *)buf, len, filename, 0);
    free(buf);
    if (JS_IsException(ret))
        mjs_dump_exception(s_ctx);
    else
        mjs_log("main.js finished");
}

void mjs_engine_run(void)
{
    if (!s_ctx) {
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char *boot = build_bootstrap();
    if (boot) {
        eval_begin();
        JSValue ret = JS_Eval(s_ctx, boot, strlen(boot), "<bootstrap>", 0);
        free(boot);
        if (JS_IsException(ret))
            mjs_dump_exception(s_ctx);
    }

    run_script();

    for (;;) {
        process_events();
        run_timers();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
