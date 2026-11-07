/* ZhengChen 1.54" TFT WiFi (ESP32-S3).
 *
 * Board-private half of the contract in main/boards/board.h.  The panel, its
 * SPI bus and the backlight PWM are declared in the board's YAML files and
 * brought up by esp_board_manager; the ST7789 driver factory lives in
 * setup_device.c next to this file. */

#include "../../board.h"

#include "config.h"

#include "driver/gpio.h"
#include "esp_board_manager_includes.h"
#include "esp_log.h"

static const board_config_t s_board = {
    .name = "1.54TFT WiFi",
    .manufacturer = "ZhengChen",
    .target = "esp32s3",
    .display_width = DISPLAY_WIDTH,
    .display_height = DISPLAY_HEIGHT,
    .led_pin = GPIO_NUM_NC,
    .boot_pin = BOOT_BUTTON_GPIO,
    /* No shared I2C bus and no register-controlled codec on this board. */
    .i2c_sda = GPIO_NUM_NC,
    .i2c_scl = GPIO_NUM_NC,
    .codec_addr = -1,
    .pa_pin = GPIO_NUM_NC,
    /* Playback pins; the microphone runs on its own I2S clock. */
    .i2s_mclk = GPIO_NUM_NC,
    .i2s_ws = AUDIO_I2S_SPK_GPIO_WS,
    .i2s_bclk = AUDIO_I2S_SPK_GPIO_BCLK,
    .i2s_din = GPIO_NUM_NC,
    .i2s_dout = AUDIO_I2S_SPK_GPIO_DOUT,
};

const board_config_t *board_get(void)
{
    return &s_board;
}

esp_err_t board_init(void)
{
    /* Claim the backlight PWM at its 0% default so the panel stays dark until
     * the JS display module raises it. */
    (void)esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS);

    gpio_config_t boot = {
        .pin_bit_mask = 1ULL << (unsigned)s_board.boot_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&boot);

    ESP_LOGI("board", "%s %s (%dx%d) [%s]", s_board.manufacturer, s_board.name,
             s_board.display_width, s_board.display_height, BOARD_ID);
    return ESP_OK;
}

/* No codec chip on this board: the audio module runs I2S only. */
const board_codec_t *board_codec(void)
{
    return NULL;
}
