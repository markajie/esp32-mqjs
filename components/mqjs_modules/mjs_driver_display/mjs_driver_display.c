#include "mjs_driver_display.h"

#include <stdlib.h>
#include <string.h>

#include "display_canvas.h"
#include "display_panel.h"
#include "esp_err.h"
#include "esp_timer.h"

static display_canvas_t s_canvas;
static bool s_canvas_ready;

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int value = def;
    if (idx < argc) {
        JS_ToInt32(ctx, &value, argv[idx]);
    }
    return value;
}

static bool arg_bool(JSContext *ctx, int argc, JSValue *argv, int idx, bool def)
{
    if (idx >= argc) {
        return def;
    }
    if (JS_IsBool(argv[idx])) {
        return argv[idx] != JS_FALSE;
    }
    int value = 0;
    if (JS_ToInt32(ctx, &value, argv[idx])) {
        return def;
    }
    return value != 0;
}

static uint16_t arg_color(JSContext *ctx, int argc, JSValue *argv, int idx, uint16_t def)
{
    return (uint16_t)(arg_int(ctx, argc, argv, idx, def) & 0xFFFF);
}

static const char *arg_text(JSContext *ctx, int argc, JSValue *argv, int idx, JSCStringBuf *buf,
                            size_t *len)
{
    if (idx >= argc || !JS_IsString(ctx, argv[idx])) {
        return NULL;
    }
    return JS_ToCStringLen(ctx, len, argv[idx], buf);
}

static bool require_canvas(JSContext *ctx)
{
    if (!s_canvas_ready) {
        mjs_throw(ctx, "display not initialized");
        return false;
    }
    return true;
}

static JSValue js_init(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)opaque;

    esp_err_t err = display_panel_open();
    if (err != ESP_OK) {
        s_canvas_ready = false;
        return mjs_throw(ctx, "display init: %s", esp_err_to_name(err));
    }
    display_canvas_attach(&s_canvas, display_panel_framebuffer(),
                          display_panel_width(), display_panel_height());
    s_canvas_ready = true;
    return JS_TRUE;
}

static JSValue js_ready(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)opaque;
    return JS_NewBool(s_canvas_ready);
}

static JSValue js_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)opaque;
    return JS_NewInt32(ctx, display_panel_is_open() ? display_panel_width() : 0);
}

static JSValue js_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)opaque;
    return JS_NewInt32(ctx, display_panel_is_open() ? display_panel_height() : 0);
}

static JSValue js_color(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    return JS_NewInt32(ctx, display_rgb565(arg_int(ctx, argc, argv, 0, 0),
                                           arg_int(ctx, argc, argv, 1, 0),
                                           arg_int(ctx, argc, argv, 2, 0)));
}

static JSValue js_present(JSContext *ctx, const char *operation)
{
    int64_t start = esp_timer_get_time();
    esp_err_t err = display_panel_present();
    if (err != ESP_OK) {
        return mjs_throw(ctx, "display %s: %s", operation, esp_err_to_name(err));
    }
    return JS_NewInt32(ctx, (int)((esp_timer_get_time() - start) / 1000));
}

static JSValue js_fill(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_clear(&s_canvas, arg_color(ctx, argc, argv, 0, 0));
    return js_present(ctx, "fill");
}

static JSValue js_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_fill_rect(&s_canvas,
                             arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0),
                             arg_int(ctx, argc, argv, 2, 0), arg_int(ctx, argc, argv, 3, 0),
                             arg_color(ctx, argc, argv, 4, 0));
    return JS_UNDEFINED;
}

static JSValue js_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_pixel(&s_canvas, arg_int(ctx, argc, argv, 0, 0),
                         arg_int(ctx, argc, argv, 1, 0), arg_color(ctx, argc, argv, 2, 0));
    return JS_UNDEFINED;
}

static JSValue js_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_line(&s_canvas, arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0),
                        arg_int(ctx, argc, argv, 2, 0), arg_int(ctx, argc, argv, 3, 0),
                        arg_color(ctx, argc, argv, 4, 0));
    return JS_UNDEFINED;
}

static JSValue js_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_rect(&s_canvas, arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0),
                        arg_int(ctx, argc, argv, 2, 0), arg_int(ctx, argc, argv, 3, 0),
                        arg_color(ctx, argc, argv, 4, 0));
    return JS_UNDEFINED;
}

static JSValue js_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    display_canvas_circle(&s_canvas, arg_int(ctx, argc, argv, 0, 0),
                          arg_int(ctx, argc, argv, 1, 0), arg_int(ctx, argc, argv, 2, 0),
                          arg_color(ctx, argc, argv, 3, 0));
    return JS_UNDEFINED;
}

static JSValue js_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    JSCStringBuf buf;
    size_t len = 0;
    const char *text = arg_text(ctx, argc, argv, 2, &buf, &len);
    if (text == NULL) {
        return JS_ThrowTypeError(ctx, "text(x, y, string, color, bg, scale)");
    }
    (void)len;
    int width = display_canvas_text(&s_canvas, arg_int(ctx, argc, argv, 0, 0),
                                    arg_int(ctx, argc, argv, 1, 0), text,
                                    arg_color(ctx, argc, argv, 3, 0xFFFF),
                                    arg_int(ctx, argc, argv, 4, -1),
                                    arg_int(ctx, argc, argv, 5, 1));
    return JS_NewInt32(ctx, width);
}

static JSValue js_text_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    JSCStringBuf buf;
    size_t len = 0;
    const char *text = arg_text(ctx, argc, argv, 0, &buf, &len);
    if (text == NULL) {
        return JS_ThrowTypeError(ctx, "textWidth(string, scale?)");
    }
    (void)len;
    return JS_NewInt32(ctx, display_canvas_text_width(text, arg_int(ctx, argc, argv, 1, 1)));
}

static JSValue js_bitmap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    int x = arg_int(ctx, argc, argv, 0, 0);
    int y = arg_int(ctx, argc, argv, 1, 0);
    int w = arg_int(ctx, argc, argv, 2, 0);
    int h = arg_int(ctx, argc, argv, 3, 0);
    if (argc < 5 || w <= 0 || h <= 0) {
        return JS_ThrowTypeError(ctx, "bitmap(x, y, w, h, data)");
    }
    size_t len = 0;
    uint8_t *data = mjs_value_to_bytes(ctx, argv[4], &len);
    if (data == NULL) {
        return JS_ThrowTypeError(ctx, "data must be a string or byte array");
    }
    esp_err_t err = display_canvas_bitmap(&s_canvas, x, y, w, h, data, len);
    free(data);
    if (err == ESP_ERR_INVALID_SIZE) {
        return JS_ThrowRangeError(ctx, "bitmap data too short");
    }
    if (err != ESP_OK) {
        return mjs_throw(ctx, "bitmap: %s", esp_err_to_name(err));
    }
    return JS_UNDEFINED;
}

static JSValue js_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)opaque;
    if (!require_canvas(ctx)) {
        return JS_EXCEPTION;
    }
    return js_present(ctx, "flush");
}

static JSValue js_backlight(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val;
    (void)opaque;
    if (!display_panel_is_open()) {
        return mjs_throw(ctx, "display not initialized");
    }
    esp_err_t err = display_panel_backlight(arg_bool(ctx, argc, argv, 0, true) ? 100 : 0);
    if (err != ESP_OK) {
        return mjs_throw(ctx, "backlight: %s", esp_err_to_name(err));
    }
    return JS_UNDEFINED;
}

void mjs_module_display_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "display");
    mjs_module_add_fn(ctx, "display", "init", js_init, NULL);
    mjs_module_add_fn(ctx, "display", "ready", js_ready, NULL);
    mjs_module_add_fn(ctx, "display", "width", js_width, NULL);
    mjs_module_add_fn(ctx, "display", "height", js_height, NULL);
    mjs_module_add_fn(ctx, "display", "color", js_color, NULL);
    mjs_module_add_fn(ctx, "display", "fill", js_fill, NULL);
    mjs_module_add_fn(ctx, "display", "clear", js_fill, NULL);
    mjs_module_add_fn(ctx, "display", "fillRect", js_fill_rect, NULL);
    mjs_module_add_fn(ctx, "display", "pixel", js_pixel, NULL);
    mjs_module_add_fn(ctx, "display", "line", js_line, NULL);
    mjs_module_add_fn(ctx, "display", "rect", js_rect, NULL);
    mjs_module_add_fn(ctx, "display", "circle", js_circle, NULL);
    mjs_module_add_fn(ctx, "display", "text", js_text, NULL);
    mjs_module_add_fn(ctx, "display", "textWidth", js_text_width, NULL);
    mjs_module_add_fn(ctx, "display", "bitmap", js_bitmap, NULL);
    mjs_module_add_fn(ctx, "display", "flush", js_flush, NULL);
    mjs_module_add_fn(ctx, "display", "present", js_flush, NULL);
    mjs_module_add_fn(ctx, "display", "backlight", js_backlight, NULL);
}
