#include "mjs_module_system.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int v = def;
    if (idx < argc)
        JS_ToInt32(ctx, &v, argv[idx]);
    return v;
}

static JSValue js_chip(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    esp_chip_info_t info;
    esp_chip_info(&info);
    const char *model = "ESP32";
#if defined(CONFIG_IDF_TARGET_ESP32C5)
    model = "ESP32-C5";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    model = "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    model = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
    model = "ESP32-P4";
#endif
    (void)info;
    return JS_NewString(ctx, model);
}

static JSValue js_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return JS_NewString(ctx, buf);
}

static JSValue js_free_heap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewInt64(ctx, esp_get_free_heap_size());
}

static JSValue js_min_heap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewInt64(ctx, esp_get_minimum_free_heap_size());
}

static JSValue js_psram(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewInt64(ctx, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static JSValue js_uptime(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
}

static JSValue js_reboot(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    mjs_log("reboot requested from JS");
    esp_restart();
    return JS_UNDEFINED;
}

static JSValue js_delay(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int ms = arg_int(ctx, argc, argv, 0, 0);
    if (ms < 0) ms = 0;
    if (ms > 60000) ms = 60000;
    vTaskDelay(pdMS_TO_TICKS(ms));
    return JS_UNDEFINED;
}

static JSValue js_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return JS_NewInt64(ctx, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static JSValue js_set_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "setTime(ms)");
    double ms = 0;
    if (JS_ToNumber(ctx, &ms, argv[0]))
        return JS_EXCEPTION;
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000.0);
    tv.tv_usec = (suseconds_t)((int64_t)ms % 1000) * 1000;
    settimeofday(&tv, NULL);
    return JS_UNDEFINED;
}

static JSValue js_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_UNDEFINED;
    JSCStringBuf sbuf;
    const char *s = JS_ToCString(ctx, argv[0], &sbuf);
    if (s)
        mjs_log("%s", s);
    return JS_UNDEFINED;
}

static JSValue js_reset_reason(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewInt32(ctx, (int)esp_reset_reason());
}

void mjs_module_system_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "system");
    mjs_module_add_fn(ctx, "system", "chip", js_chip, NULL);
    mjs_module_add_fn(ctx, "system", "mac", js_mac, NULL);
    mjs_module_add_fn(ctx, "system", "freeHeap", js_free_heap, NULL);
    mjs_module_add_fn(ctx, "system", "minFreeHeap", js_min_heap, NULL);
    mjs_module_add_fn(ctx, "system", "freePsram", js_psram, NULL);
    mjs_module_add_fn(ctx, "system", "uptime", js_uptime, NULL);
    mjs_module_add_fn(ctx, "system", "reboot", js_reboot, NULL);
    mjs_module_add_fn(ctx, "system", "delay", js_delay, NULL);
    mjs_module_add_fn(ctx, "system", "time", js_time, NULL);
    mjs_module_add_fn(ctx, "system", "setTime", js_set_time, NULL);
    mjs_module_add_fn(ctx, "system", "log", js_log, NULL);
    mjs_module_add_fn(ctx, "system", "resetReason", js_reset_reason, NULL);
}
