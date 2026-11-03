#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Owns the direct esp_lcd panel path, the PSRAM framebuffer and the board
 * backlight.  The panel init table and gamma/inversion stay in the board
 * manager; this file only stages RGB565 bytes into an internal DMA buffer. */

esp_err_t display_panel_open(void);
bool display_panel_is_open(void);
uint16_t *display_panel_framebuffer(void);
uint16_t display_panel_width(void);
uint16_t display_panel_height(void);
esp_err_t display_panel_present(void);
esp_err_t display_panel_backlight(uint8_t percent);
