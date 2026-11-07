#include "../../board.h"

#include "config.h"

#include "driver/gpio.h"
#include "esp_board_manager_includes.h"
#include "esp_log.h"

static const board_config_t s_board = {
    .name = "Moji 2",
    .manufacturer = "Movecall",
    .target = "esp32c5",
    .display_width = DISPLAY_WIDTH,
    .display_height = DISPLAY_HEIGHT,
    .led_pin = BUILTIN_LED_GPIO,
    .boot_pin = BOOT_BUTTON_GPIO,
    .i2c_sda = AUDIO_CODEC_I2C_SDA_PIN,
    .i2c_scl = AUDIO_CODEC_I2C_SCL_PIN,
    .codec_addr = AUDIO_CODEC_ES8311_ADDR,
    .pa_pin = AUDIO_CODEC_PA_PIN,
    .i2s_mclk = AUDIO_I2S_GPIO_MCLK,
    .i2s_ws = AUDIO_I2S_GPIO_WS,
    .i2s_bclk = AUDIO_I2S_GPIO_BCLK,
    .i2s_din = AUDIO_I2S_GPIO_DIN,
    .i2s_dout = AUDIO_I2S_GPIO_DOUT,
};

const board_config_t *board_get(void)
{
    return &s_board;
}

esp_err_t board_init(void)
{
    /* Claim the backlight PWM early at its 0% default so the panel stays dark
     * until display.init() raises it; the board manager owns the pin. */
    (void)esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS);

    gpio_config_t led = {
        .pin_bit_mask = 1ULL << s_board.led_pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(s_board.led_pin, 0);

    gpio_config_t boot = {
        .pin_bit_mask = 1ULL << s_board.boot_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&boot);

    ESP_LOGI("board", "%s %s (%dx%d)", s_board.manufacturer, s_board.name,
             s_board.display_width, s_board.display_height);
    return ESP_OK;
}
