#include "display_canvas.h"

#include <string.h>

#include "font_atlas.h"

#define DISPLAY_SCALE_MIN 1
#define DISPLAY_SCALE_MAX 4

static inline int clamp_scale(int scale)
{
    if (scale < DISPLAY_SCALE_MIN) return DISPLAY_SCALE_MIN;
    if (scale > DISPLAY_SCALE_MAX) return DISPLAY_SCALE_MAX;
    return scale;
}

static inline void put_px(display_canvas_t *canvas, int x, int y, uint16_t color)
{
    if ((unsigned)x >= (unsigned)canvas->width || (unsigned)y >= (unsigned)canvas->height) {
        return;
    }
    canvas->pixels[(size_t)y * (size_t)canvas->width + (size_t)x] = color;
}

static void blend_px(display_canvas_t *canvas, int x, int y, uint16_t fg, int alpha)
{
    if (alpha <= 0 || (unsigned)x >= (unsigned)canvas->width ||
        (unsigned)y >= (unsigned)canvas->height) {
        return;
    }
    uint16_t *dst = &canvas->pixels[(size_t)y * (size_t)canvas->width + (size_t)x];
    if (alpha >= 255) {
        *dst = fg;
        return;
    }
    int dr = *dst >> 11, dg = (*dst >> 5) & 0x3F, db = *dst & 0x1F;
    int fr = fg >> 11, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int r = (fr * alpha + dr * (255 - alpha)) >> 8;
    int g = (fgc * alpha + dg * (255 - alpha)) >> 8;
    int b = (fb * alpha + db * (255 - alpha)) >> 8;
    *dst = (uint16_t)((r << 11) | (g << 5) | b);
}

void display_canvas_attach(display_canvas_t *canvas, uint16_t *pixels, int width, int height)
{
    canvas->pixels = pixels;
    canvas->width = width;
    canvas->height = height;
}

void display_canvas_clear(display_canvas_t *canvas, uint16_t color)
{
    if (canvas->pixels == NULL) return;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; i++) {
        canvas->pixels[i] = color;
    }
}

void display_canvas_fill_rect(display_canvas_t *canvas, int x, int y, int w, int h, uint16_t color)
{
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > canvas->width) x1 = canvas->width;
    if (y1 > canvas->height) y1 = canvas->height;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &canvas->pixels[(size_t)yy * (size_t)canvas->width];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = color;
        }
    }
}

void display_canvas_pixel(display_canvas_t *canvas, int x, int y, uint16_t color)
{
    put_px(canvas, x, y, color);
}

void display_canvas_line(display_canvas_t *canvas, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put_px(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void display_canvas_rect(display_canvas_t *canvas, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x + w - 1, y1 = y + h - 1;
    display_canvas_line(canvas, x, y, x1, y, color);
    display_canvas_line(canvas, x, y1, x1, y1, color);
    display_canvas_line(canvas, x, y, x, y1, color);
    display_canvas_line(canvas, x1, y, x1, y1, color);
}

void display_canvas_circle(display_canvas_t *canvas, int cx, int cy, int r, uint16_t color)
{
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        put_px(canvas, cx + x, cy + y, color);
        put_px(canvas, cx + y, cy + x, color);
        put_px(canvas, cx - y, cy + x, color);
        put_px(canvas, cx - x, cy + y, color);
        put_px(canvas, cx - x, cy - y, color);
        put_px(canvas, cx - y, cy - x, color);
        put_px(canvas, cx + y, cy - x, color);
        put_px(canvas, cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static int glyph_index(uint32_t cp)
{
    int lo = 0, hi = FONT_ATLAS_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_cp[mid] == cp) return mid;
        if (font_cp[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static int big_glyph_index(uint32_t cp)
{
    for (int i = 0; i < FONT_BIG_COUNT; i++) {
        if (font_big_cp[i] == cp) return i;
    }
    return -1;
}

static const uint8_t *utf8_next(const uint8_t *p, const uint8_t *end, uint32_t *cp)
{
    uint8_t c = *p++;
    if (c < 0x80) {
        *cp = c;
        return p;
    }
    int extra;
    uint32_t value;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        value = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        value = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        value = c & 0x07;
    } else {
        *cp = 0xFFFD;
        return p;
    }
    if (p + extra > end) {
        *cp = 0xFFFD;
        return end;
    }
    for (int i = 0; i < extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return p;
        }
        value = (value << 6) | (p[i] & 0x3F);
    }
    *cp = value;
    return p + extra;
}

static int glyph_advance(int index, uint32_t cp)
{
    if (index >= 0 && font_adv[index] != 0) return font_adv[index];
    return cp < 0x80 ? 8 : FONT_ATLAS_CELL;
}

static void draw_glyph(display_canvas_t *canvas, int x, int y, int index, int scale,
                       uint16_t fg, int bg, int advance)
{
    if (bg >= 0) {
        display_canvas_fill_rect(canvas, x, y, advance * scale, FONT_ATLAS_CELL * scale, (uint16_t)bg);
    }
    const uint8_t *bits = font_bits + font_off[index];
    for (int yy = 0; yy < FONT_ATLAS_CELL; yy++) {
        for (int xx = 0; xx < FONT_ATLAS_CELL; xx++) {
            if (!(bits[yy * 4 + (xx >> 3)] & (0x80 >> (xx & 7)))) continue;
            display_canvas_fill_rect(canvas, x + xx * scale, y + yy * scale, scale, scale, fg);
        }
    }
}

/* Scale 4 draws the 96 px raster 1:1; scale 2 downsamples it 2x2 so large
 * digits keep their anti-aliased edges instead of magnifying the 24 px bitmap. */
static void draw_glyph_big(display_canvas_t *canvas, int x, int y, int index, int scale,
                           uint16_t fg, int bg, int advance)
{
    if (bg >= 0) {
        display_canvas_fill_rect(canvas, x, y, advance * scale, FONT_ATLAS_CELL * scale, (uint16_t)bg);
    }
    const uint8_t *bits = font_big_bits + font_big_off[index];
    if (scale == 4) {
        for (int yy = 0; yy < FONT_BIG_CELL; yy++) {
            for (int xx = 0; xx < FONT_BIG_CELL; xx++) {
                int alpha = bits[yy * FONT_BIG_CELL + xx];
                if (alpha != 0) blend_px(canvas, x + xx, y + yy, fg, alpha);
            }
        }
        return;
    }
    for (int yy = 0; yy < FONT_BIG_CELL / 2; yy++) {
        for (int xx = 0; xx < FONT_BIG_CELL / 2; xx++) {
            const uint8_t *q = bits + (yy * 2) * FONT_BIG_CELL + xx * 2;
            int alpha = (q[0] + q[1] + q[FONT_BIG_CELL] + q[FONT_BIG_CELL + 1] + 2) >> 2;
            if (alpha != 0) blend_px(canvas, x + xx, y + yy, fg, alpha);
        }
    }
}

static void draw_missing_glyph(display_canvas_t *canvas, int x, int y, int scale, uint16_t fg)
{
    int size = FONT_ATLAS_CELL * scale;
    int inner = (FONT_ATLAS_CELL - 1) * scale;
    for (int yy = 0; yy < size; yy++) {
        if (yy < scale || yy >= inner) {
            display_canvas_fill_rect(canvas, x, y + yy, size, 1, fg);
        } else {
            display_canvas_fill_rect(canvas, x, y + yy, scale, 1, fg);
            display_canvas_fill_rect(canvas, x + inner, y + yy, scale, 1, fg);
        }
    }
}

static int text_extent(const char *text, bool draw, display_canvas_t *canvas,
                       int x_start, int y_start, uint16_t fg, int bg, int scale)
{
    int x = x_start;
    int line_width = 0;
    int widest = 0;
    const uint8_t *p = (const uint8_t *)text;
    const uint8_t *end = p + strlen(text);

    while (p < end) {
        uint32_t cp;
        p = utf8_next(p, end, &cp);
        if (cp == '\n') {
            if (line_width > widest) widest = line_width;
            x = x_start;
            y_start += FONT_ATLAS_LINE * scale;
            line_width = 0;
            continue;
        }
        if (cp < 0x20 || cp == 0x7F) cp = '?';
        int index = glyph_index(cp);
        int advance = glyph_advance(index, cp);
        if (draw) {
            int big = (scale == 2 || scale == 4) ? big_glyph_index(cp) : -1;
            if (big >= 0) {
                advance = font_big_adv[big];
                draw_glyph_big(canvas, x, y_start, big, scale, fg, bg, advance);
            } else if (index >= 0) {
                draw_glyph(canvas, x, y_start, index, scale, fg, bg, advance);
            } else {
                draw_missing_glyph(canvas, x, y_start, scale, fg);
                advance = FONT_ATLAS_CELL;
            }
        }
        x += advance * scale;
        line_width += advance * scale;
    }
    if (line_width > widest) widest = line_width;
    return widest;
}

int display_canvas_text(display_canvas_t *canvas, int x, int y, const char *text,
                        uint16_t fg, int bg, int scale)
{
    scale = clamp_scale(scale);
    return text_extent(text, true, canvas, x, y, fg, bg, scale);
}

int display_canvas_text_width(const char *text, int scale)
{
    scale = clamp_scale(scale);
    return text_extent(text, false, NULL, 0, 0, 0, -1, scale);
}

esp_err_t display_canvas_bitmap(display_canvas_t *canvas, int x, int y, int w, int h,
                                const uint8_t *data, size_t len)
{
    if (w <= 0 || h <= 0) return ESP_ERR_INVALID_ARG;
    if (len < (size_t)w * (size_t)h * 2) return ESP_ERR_INVALID_SIZE;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > canvas->width ? canvas->width : x + w;
    int y1 = y + h > canvas->height ? canvas->height : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *dst = &canvas->pixels[(size_t)yy * (size_t)canvas->width];
        const uint8_t *src = data + ((size_t)(yy - y) * (size_t)w + (size_t)(x0 - x)) * 2;
        for (int xx = x0; xx < x1; xx++) {
            uint16_t value;
            memcpy(&value, src, sizeof(value));
            dst[xx] = value;
            src += 2;
        }
    }
    return ESP_OK;
}
