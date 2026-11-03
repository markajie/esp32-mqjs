#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *manufacturer;
    const char *target;

    int display_width;
    int display_height;

    int led_pin;
    int boot_pin;

    int i2c_sda;
    int i2c_scl;
    int codec_addr;
    int pa_pin;

    int i2s_mclk;
    int i2s_ws;
    int i2s_bclk;
    int i2s_din;
    int i2s_dout;
} board_config_t;

const board_config_t *board_get(void);

/* Early board bring-up: LED and button.  Display, backlight PWM and audio
 * belong to the board manager (the board's YAML files). */
esp_err_t board_init(void);

#ifdef __cplusplus
}
#endif
