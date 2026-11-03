#include "mjs_driver_i2c.h"

#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#define MAX_DEVICES 8

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;
static uint32_t s_hz = 100000;
static int s_sda = -1, s_scl = -1;

typedef struct {
    uint16_t addr;
    i2c_master_dev_handle_t dev;
} dev_entry_t;

static dev_entry_t s_devs[MAX_DEVICES];
static int s_dev_count;

bool mjs_i2c_ready(void)
{
    return s_bus != NULL;
}

esp_err_t mjs_i2c_init(int sda, int scl, uint32_t hz)
{
    if (s_bus)
        return ESP_OK;
    s_sda = sda;
    s_scl = scl;
    s_hz = hz ? hz : 100000;
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
    }
    return err;
}

static i2c_master_dev_handle_t get_dev(uint8_t addr)
{
    if (!s_bus)
        return NULL;
    for (int i = 0; i < s_dev_count; i++) {
        if (s_devs[i].addr == addr)
            return s_devs[i].dev;
    }
    if (s_dev_count >= MAX_DEVICES)
        return NULL;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = s_hz,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(s_bus, &cfg, &dev) != ESP_OK)
        return NULL;
    s_devs[s_dev_count].addr = addr;
    s_devs[s_dev_count].dev = dev;
    s_dev_count++;
    return dev;
}

esp_err_t mjs_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = get_dev(addr);
    if (!dev)
        return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(dev, data, len, 1000);
}

esp_err_t mjs_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = get_dev(addr);
    if (!dev)
        return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(dev, data, len, 1000);
}

esp_err_t mjs_i2c_write_read(uint8_t addr, const uint8_t *wbuf, size_t wlen,
                             uint8_t *rbuf, size_t rlen)
{
    i2c_master_dev_handle_t dev = get_dev(addr);
    if (!dev)
        return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(dev, wbuf, wlen, rbuf, rlen, 1000);
}

esp_err_t mjs_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return mjs_i2c_write(addr, buf, sizeof(buf));
}

esp_err_t mjs_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    return mjs_i2c_write_read(addr, &reg, 1, val, 1);
}

int mjs_i2c_probe(uint8_t addr)
{
    if (!s_bus)
        return 0;
    return i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}

/* ---- JS bindings ------------------------------------------------------- */

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int v = def;
    if (idx < argc)
        JS_ToInt32(ctx, &v, argv[idx]);
    return v;
}

static JSValue js_init(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int sda = arg_int(ctx, argc, argv, 0, -1);
    int scl = arg_int(ctx, argc, argv, 1, -1);
    int hz = arg_int(ctx, argc, argv, 2, 100000);
    if (sda < 0 || scl < 0)
        return JS_ThrowTypeError(ctx, "i2c.init(sda, scl, hz)");
    esp_err_t err = mjs_i2c_init(sda, scl, (uint32_t)hz);
    if (err != ESP_OK)
        return mjs_throw(ctx, "i2c init: %s", esp_err_to_name(err));
    return JS_TRUE;
}

static JSValue js_ready(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(mjs_i2c_ready());
}

static JSValue js_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int addr = arg_int(ctx, argc, argv, 0, -1);
    if (addr < 0 || argc < 2)
        return JS_ThrowTypeError(ctx, "i2c.write(addr, data)");
    size_t len = 0;
    uint8_t *data = mjs_value_to_bytes(ctx, argv[1], &len);
    if (!data)
        return JS_ThrowTypeError(ctx, "data must be a string or array");
    esp_err_t err = mjs_i2c_write((uint8_t)addr, data, len);
    free(data);
    if (err != ESP_OK)
        return mjs_throw(ctx, "i2c write 0x%02x: %s", addr, esp_err_to_name(err));
    return JS_NewInt32(ctx, (int)len);
}

static JSValue js_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int addr = arg_int(ctx, argc, argv, 0, -1);
    int len = arg_int(ctx, argc, argv, 1, 0);
    if (addr < 0 || len <= 0 || len > 4096)
        return JS_ThrowRangeError(ctx, "i2c.read(addr, 1..4096)");
    uint8_t *buf = malloc((size_t)len);
    if (!buf)
        return JS_ThrowInternalError(ctx, "out of memory");
    esp_err_t err = mjs_i2c_read((uint8_t)addr, buf, (size_t)len);
    if (err != ESP_OK) {
        free(buf);
        return mjs_throw(ctx, "i2c read 0x%02x: %s", addr, esp_err_to_name(err));
    }
    JSValue ret = mjs_new_byte_array(ctx, buf, (size_t)len);
    free(buf);
    return ret;
}

static JSValue js_write_reg(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int addr = arg_int(ctx, argc, argv, 0, -1);
    int reg = arg_int(ctx, argc, argv, 1, -1);
    if (addr < 0 || reg < 0 || argc < 3)
        return JS_ThrowTypeError(ctx, "i2c.writeReg(addr, reg, data)");
    size_t len = 0;
    uint8_t *data = mjs_value_to_bytes(ctx, argv[2], &len);
    if (!data)
        return JS_ThrowTypeError(ctx, "data must be a string or array");
    uint8_t *buf = malloc(len + 1);
    if (!buf) {
        free(data);
        return JS_ThrowInternalError(ctx, "out of memory");
    }
    buf[0] = (uint8_t)reg;
    memcpy(buf + 1, data, len);
    esp_err_t err = mjs_i2c_write((uint8_t)addr, buf, len + 1);
    free(buf);
    free(data);
    if (err != ESP_OK)
        return mjs_throw(ctx, "i2c writeReg: %s", esp_err_to_name(err));
    return JS_NewInt32(ctx, (int)(len + 1));
}

static JSValue js_read_reg(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int addr = arg_int(ctx, argc, argv, 0, -1);
    int reg = arg_int(ctx, argc, argv, 1, -1);
    int len = arg_int(ctx, argc, argv, 2, 1);
    if (addr < 0 || reg < 0 || len <= 0 || len > 4096)
        return JS_ThrowRangeError(ctx, "i2c.readReg(addr, reg, len)");
    uint8_t *buf = malloc((size_t)len);
    if (!buf)
        return JS_ThrowInternalError(ctx, "out of memory");
    esp_err_t err = mjs_i2c_write_read((uint8_t)addr, (uint8_t *)&reg, 1, buf, (size_t)len);
    if (err != ESP_OK) {
        free(buf);
        return mjs_throw(ctx, "i2c readReg: %s", esp_err_to_name(err));
    }
    JSValue ret = mjs_new_byte_array(ctx, buf, (size_t)len);
    free(buf);
    return ret;
}

static JSValue js_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    JSGCRef a_ref;
    JSValue *a = JS_PushGCRef(ctx, &a_ref);
    *a = JS_NewArray(ctx, 0);
    int n = 0;
    for (int addr = 1; addr < 127; addr++) {
        if (mjs_i2c_probe((uint8_t)addr))
            JS_SetPropertyUint32(ctx, *a, (uint32_t)n++, JS_NewInt32(ctx, addr));
    }
    JSValue ret = *a;
    JS_PopGCRef(ctx, &a_ref);
    return ret;
}

void mjs_driver_i2c_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "i2c");
    mjs_module_add_fn(ctx, "i2c", "init", js_init, NULL);
    mjs_module_add_fn(ctx, "i2c", "ready", js_ready, NULL);
    mjs_module_add_fn(ctx, "i2c", "write", js_write, NULL);
    mjs_module_add_fn(ctx, "i2c", "read", js_read, NULL);
    mjs_module_add_fn(ctx, "i2c", "writeReg", js_write_reg, NULL);
    mjs_module_add_fn(ctx, "i2c", "readReg", js_read_reg, NULL);
    mjs_module_add_fn(ctx, "i2c", "scan", js_scan, NULL);
}
