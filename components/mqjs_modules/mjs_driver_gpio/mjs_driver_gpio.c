#include "mjs_driver_gpio.h"

#include "driver/gpio.h"

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int v = def;
    if (idx < argc)
        JS_ToInt32(ctx, &v, argv[idx]);
    return v;
}

static bool valid_pin(JSContext *ctx, int pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        JS_ThrowRangeError(ctx, "invalid GPIO %d", pin);
        return false;
    }
    return true;
}

static JSValue js_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int pin = arg_int(ctx, argc, argv, 0, -1);
    int mode = arg_int(ctx, argc, argv, 1, 1);
    if (!valid_pin(ctx, pin))
        return JS_EXCEPTION;
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << pin };
    switch (mode) {
    case 0: cfg.mode = GPIO_MODE_INPUT; break;
    case 1: cfg.mode = GPIO_MODE_OUTPUT; break;
    case 2: cfg.mode = GPIO_MODE_INPUT; cfg.pull_up_en = 1; break;
    case 3: cfg.mode = GPIO_MODE_INPUT; cfg.pull_down_en = 1; break;
    case 4: cfg.mode = GPIO_MODE_OUTPUT_OD; break;
    default: return JS_ThrowRangeError(ctx, "mode 0..4");
    }
    if (gpio_config(&cfg) != ESP_OK)
        return mjs_throw(ctx, "gpio_config(%d) failed", pin);
    return JS_UNDEFINED;
}

static JSValue js_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int pin = arg_int(ctx, argc, argv, 0, -1);
    int level = arg_int(ctx, argc, argv, 1, 0);
    if (!valid_pin(ctx, pin))
        return JS_EXCEPTION;
    gpio_set_level(pin, level ? 1 : 0);
    return JS_UNDEFINED;
}

static JSValue js_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int pin = arg_int(ctx, argc, argv, 0, -1);
    if (!valid_pin(ctx, pin))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, gpio_get_level(pin));
}

static JSValue js_toggle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int pin = arg_int(ctx, argc, argv, 0, -1);
    if (!valid_pin(ctx, pin))
        return JS_EXCEPTION;
    int v = gpio_get_level(pin);
    gpio_set_level(pin, v ? 0 : 1);
    return JS_NewInt32(ctx, v ? 0 : 1);
}

void mjs_driver_gpio_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "gpio");
    mjs_module_add_fn(ctx, "gpio", "mode", js_mode, NULL);
    mjs_module_add_fn(ctx, "gpio", "write", js_write, NULL);
    mjs_module_add_fn(ctx, "gpio", "read", js_read, NULL);
    mjs_module_add_fn(ctx, "gpio", "toggle", js_toggle, NULL);
}
