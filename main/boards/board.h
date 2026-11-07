#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared board contract.  Every main/boards/<vendor>/<board>/board.c
 * implements it; main.c and the shared modules only use this struct, so a new
 * board never has to change code outside its own directory.  Use GPIO_NUM_NC
 * (or -1) for hardware a board does not have. */
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

/* I2C access supplied by the shared audio module, so a board-owned codec
 * driver needs no shared-component headers. */
typedef struct {
    uint8_t address;                                            /* 7-bit I2C address */
    esp_err_t (*write_reg)(uint8_t address, uint8_t reg, uint8_t value);
    esp_err_t (*read_reg)(uint8_t address, uint8_t reg, uint8_t *value);
} board_codec_bus_t;

/* Optional audio codec.  board_codec() returns NULL when the board has no
 * codec chip; the shared audio module then runs I2S only.  The codec's
 * register sequence, volume and mute live in the board directory. */
typedef struct {
    const char *name;
    esp_err_t (*init)(const board_codec_bus_t *bus);
    esp_err_t (*set_volume)(uint8_t percent);
    esp_err_t (*mute)(bool on);
} board_codec_t;

const board_codec_t *board_codec(void);

#ifdef __cplusplus
}
#endif
