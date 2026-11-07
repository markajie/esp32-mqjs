#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* RGB565 framebuffer drawing.  Pixels are native little-endian RGB565; the
 * panel byte order is display_panel's DMA-staging concern, never the script's. */
typedef struct {
    uint16_t *pixels;
    int width;
    int height;
} display_canvas_t;

static inline uint16_t display_rgb565(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void display_canvas_attach(display_canvas_t *canvas, uint16_t *pixels, int width, int height);
void display_canvas_clear(display_canvas_t *canvas, uint16_t color);
void display_canvas_fill_rect(display_canvas_t *canvas, int x, int y, int w, int h, uint16_t color);
void display_canvas_pixel(display_canvas_t *canvas, int x, int y, uint16_t color);
void display_canvas_line(display_canvas_t *canvas, int x0, int y0, int x1, int y1, uint16_t color);
void display_canvas_rect(display_canvas_t *canvas, int x, int y, int w, int h, uint16_t color);
void display_canvas_circle(display_canvas_t *canvas, int cx, int cy, int r, uint16_t color);

/* Draw an UTF-8 string; bg < 0 is transparent.  Scale is clamped to 1..4.
 * Returns the widest line in pixels. */
int display_canvas_text(display_canvas_t *canvas, int x, int y, const char *text,
                        uint16_t fg, int bg, int scale);
int display_canvas_text_width(const char *text, int scale);

/* Copy a tightly packed little-endian RGB565 image, clipped to the canvas. */
esp_err_t display_canvas_bitmap(display_canvas_t *canvas, int x, int y, int w, int h,
                                const uint8_t *data, size_t len);
