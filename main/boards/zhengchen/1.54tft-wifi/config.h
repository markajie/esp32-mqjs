#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

/* ZhengChen 1.54" TFT WiFi board (ESP32-S3).
 *
 * Only pins and panel geometry live here.  The SPI bus, the ST7789 panel and
 * the backlight PWM are described in board_peripherals.yaml /
 * board_devices.yaml, so the board manager owns their bring-up. */

#include <driver/gpio.h>

#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

/* Buttons */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_10
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

/* Speaker side of the I2S link (this board has no codec chip). */
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_WS   GPIO_NUM_16
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7

/* Microphone side (independent I2S clock). */
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

/* Backlight PWM */
#define DISPLAY_BACKLIGHT_GPIO  GPIO_NUM_20

#endif // _BOARD_CONFIG_H_
