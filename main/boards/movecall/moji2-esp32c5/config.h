#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Movecall Moji 2 configuration

#include <driver/gpio.h>

enum PowerSupply {
    kDeviceTypecSupply,
    kDeviceBatterySupply,
};

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK         GPIO_NUM_25
#define AUDIO_I2S_GPIO_WS           GPIO_NUM_24
#define AUDIO_I2S_GPIO_BCLK         GPIO_NUM_11
#define AUDIO_I2S_GPIO_DIN          GPIO_NUM_12
#define AUDIO_I2S_GPIO_DOUT         GPIO_NUM_23

#define AUDIO_CODEC_PA_PIN          GPIO_NUM_5
#define AUDIO_CODEC_I2C_SDA_PIN     GPIO_NUM_26
#define AUDIO_CODEC_I2C_SCL_PIN     GPIO_NUM_27
#define AUDIO_CODEC_ES8311_ADDR     0x18  /* 7-bit address; esp_codec_dev calls it 0x30 */

#define BUILTIN_LED_GPIO            GPIO_NUM_10
#define BOOT_BUTTON_GPIO            GPIO_NUM_28

/* The display panel, its QSPI pins, RGB565 byte order and vendor init table
 * live in board_devices.yaml / board_peripherals.yaml + setup_device.c and are
 * owned by esp_board_manager.  Only the advertised resolution stays here for
 * the JS `board` object. */
#define DISPLAY_WIDTH               360
#define DISPLAY_HEIGHT              360

#endif // _BOARD_CONFIG_H_
