#include "mjs_module_fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_spiffs.h"

static const char *path_arg(JSContext *ctx, JSValue *argv, int argc, JSCStringBuf *buf)
{
    if (argc < 1) {
        JS_ThrowTypeError(ctx, "path expected");
        return NULL;
    }
    return JS_ToCString(ctx, argv[0], buf);
}

static JSValue js_exists(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    struct stat st;
    return JS_NewBool(stat(path, &st) == 0);
}

static JSValue js_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    size_t len = 0;
    uint8_t *data = mjs_read_file(path, &len);
    if (!data)
        return JS_ThrowInternalError(ctx, "cannot read %s", path);
    if (len > 256 * 1024) {
        free(data);
        return JS_ThrowRangeError(ctx, "file too large for JS heap");
    }
    JSValue ret = JS_NewStringLen(ctx, (const char *)data, len);
    free(data);
    return ret;
}

static JSValue js_read_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    size_t len = 0;
    uint8_t *data = mjs_read_file(path, &len);
    if (!data)
        return JS_ThrowInternalError(ctx, "cannot read %s", path);
    if (len > 256 * 1024) {
        free(data);
        return JS_ThrowRangeError(ctx, "file too large for JS heap");
    }
    JSValue ret = mjs_new_byte_array(ctx, data, len);
    free(data);
    return ret;
}

static JSValue js_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "write(path, data)");
    size_t len = 0;
    uint8_t *data = mjs_value_to_bytes(ctx, argv[1], &len);
    if (!data && len != 0)
        return JS_ThrowTypeError(ctx, "data must be a string or array");
    bool ok = mjs_write_file(path, data ? data : (const uint8_t *)"", len);
    free(data);
    if (!ok)
        return JS_ThrowInternalError(ctx, "cannot write %s", path);
    return JS_NewInt32(ctx, (int32_t)len);
}

static JSValue js_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    return JS_NewBool(unlink(path) == 0);
}

static JSValue js_rename(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf a, b;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "rename(old, new)");
    const char *from = JS_ToCString(ctx, argv[0], &a);
    const char *to = JS_ToCString(ctx, argv[1], &b);
    if (!from || !to)
        return JS_EXCEPTION;
    return JS_NewBool(rename(from, to) == 0);
}

static JSValue js_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = path_arg(ctx, argv, argc, &buf);
    if (!path)
        return JS_EXCEPTION;
    struct stat st;
    if (stat(path, &st) != 0)
        return JS_NewInt32(ctx, -1);
    return JS_NewInt64(ctx, (int64_t)st.st_size);
}

static JSValue js_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf buf;
    const char *path = "/spiffs";
    if (argc >= 1) {
        path = JS_ToCString(ctx, argv[0], &buf);
        if (!path)
            return JS_EXCEPTION;
    }
    DIR *d = opendir(path);
    if (!d)
        return JS_ThrowInternalError(ctx, "cannot open %s", path);

    JSGCRef arr_ref, name_ref;
    JSValue *arr = JS_PushGCRef(ctx, &arr_ref);
    JSValue *name = JS_PushGCRef(ctx, &name_ref);
    *arr = JS_NewArray(ctx, 0);
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;
        *name = JS_NewString(ctx, ent->d_name);
        if (!JS_IsException(*name))
            JS_SetPropertyUint32(ctx, *arr, (uint32_t)n++, *name);
    }
    closedir(d);
    JSValue ret = *arr;
    JS_PopGCRef(ctx, &name_ref);
    JS_PopGCRef(ctx, &arr_ref);
    return ret;
}

static JSValue js_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);

    JSGCRef o_ref;
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *o = JS_NewObject(ctx);
    if (!JS_IsException(*o)) {
        JS_SetPropertyStr(ctx, *o, "total", JS_NewInt64(ctx, total));
        JS_SetPropertyStr(ctx, *o, "used", JS_NewInt64(ctx, used));
        JS_SetPropertyStr(ctx, *o, "free", JS_NewInt64(ctx, total - used));
    }
    JSValue ret = *o;
    JS_PopGCRef(ctx, &o_ref);
    return ret;
}

void mjs_module_fs_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "fs");
    mjs_module_add_fn(ctx, "fs", "exists", js_exists, NULL);
    mjs_module_add_fn(ctx, "fs", "read", js_read, NULL);
    mjs_module_add_fn(ctx, "fs", "readBytes", js_read_bytes, NULL);
    mjs_module_add_fn(ctx, "fs", "write", js_write, NULL);
    mjs_module_add_fn(ctx, "fs", "remove", js_remove, NULL);
    mjs_module_add_fn(ctx, "fs", "rename", js_rename, NULL);
    mjs_module_add_fn(ctx, "fs", "size", js_size, NULL);
    mjs_module_add_fn(ctx, "fs", "list", js_list, NULL);
    mjs_module_add_fn(ctx, "fs", "info", js_info, NULL);
}
