#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board.h"
#include "mjs_engine.h"
#include "mjs_module_system.h"
#include "mjs_module_fs.h"
#include "mjs_driver_gpio.h"
#include "mjs_driver_i2c.h"
#include "mjs_driver_display.h"
#include "mjs_module_audio.h"
#include "mjs_module_wifi.h"
#include "mjs_module_net.h"
#include "mjs_module_ble.h"
#include "mjs_module_web.h"
#include "mjs_module_mqtt.h"
#include "mjs_module_ws.h"

extern const uint8_t _binary_main_js_start[];
extern const uint8_t _binary_main_js_end[];

static const char *TAG = "app";

static void nvs_boot(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot");
    nvs_boot();
    ESP_ERROR_CHECK(board_init());
    const board_config_t *b = board_get();

    /* Hold BOOT while resetting to bypass user.js and recover via main.js. */
    if (gpio_get_level(b->boot_pin) == 0) {
        ESP_LOGW(TAG, "BOOT held: skipping /spiffs/user.js");
        mjs_engine_set_user_script_enabled(false);
    }

    esp_err_t err = mjs_engine_init(512 * 1024);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JS engine init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Shared I2C bus: touch, codec, and user devices.  Failure is not fatal. */
    mjs_i2c_init(b->i2c_sda, b->i2c_scl, 100000);

    JSContext *ctx = mjs_engine_ctx();
    mjs_module_system_init(ctx);
    mjs_module_fs_init(ctx);
    mjs_driver_gpio_init(ctx);
    mjs_driver_i2c_init(ctx);

    mjs_audio_pins_t audio = {
        .mclk = b->i2s_mclk, .ws = b->i2s_ws, .bclk = b->i2s_bclk,
        .din = b->i2s_din, .dout = b->i2s_dout, .pa = b->pa_pin,
        .i2c_sda = b->i2c_sda, .i2c_scl = b->i2c_scl, .codec_addr = b->codec_addr,
    };
    mjs_module_audio_init(ctx, &audio);

    mjs_module_display_init(ctx);
    mjs_module_wifi_init(ctx);
    mjs_module_net_init(ctx);
    mjs_module_ble_init(ctx);
    mjs_module_web_init(ctx);
    mjs_module_mqtt_init(ctx);
    mjs_module_ws_init(ctx);

    mjs_module_begin(ctx, "board");
    mjs_module_add_str(ctx, "board", "name", b->name);
    mjs_module_add_str(ctx, "board", "manufacturer", b->manufacturer);
    mjs_module_add_str(ctx, "board", "target", b->target);
    mjs_module_add_int(ctx, "board", "width", b->display_width);
    mjs_module_add_int(ctx, "board", "height", b->display_height);
    mjs_module_add_int(ctx, "board", "ledPin", b->led_pin);
    mjs_module_add_int(ctx, "board", "bootPin", b->boot_pin);

    mjs_engine_set_embedded_script(_binary_main_js_start,
                                   (size_t)(_binary_main_js_end - _binary_main_js_start));
    mjs_engine_run();
}
