#include "display_panel.h"

#include <string.h>

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "mjs_display";

#define DISPLAY_STRIP_BYTES (4 * 1024)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_framebuffer;
static uint8_t *s_strip;
static size_t s_strip_bytes;
static uint16_t s_width;
static uint16_t s_height;
static bool s_swap_rgb565;
static SemaphoreHandle_t s_flush_done;
static periph_ledc_handle_t *s_backlight;
static uint32_t s_backlight_max_duty;

static bool display_panel_color_done(esp_lcd_panel_io_handle_t io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

esp_err_t display_panel_open(void)
{
    if (s_panel != NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display device init failed: %s", esp_err_to_name(err));
        return err;
    }

    void *handle = NULL;
    void *config = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &handle),
                        TAG, "get display handle failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &config),
                        TAG, "get display config failed");

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)handle;
    dev_display_lcd_config_t *lcd_config = (dev_display_lcd_config_t *)config;
    ESP_RETURN_ON_FALSE(lcd_handles != NULL && lcd_config != NULL && lcd_handles->panel_handle != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "invalid display handles");
    ESP_RETURN_ON_FALSE(lcd_config->bits_per_pixel == 16,
                        ESP_ERR_NOT_SUPPORTED, TAG, "unsupported panel depth: %u",
                        (unsigned)lcd_config->bits_per_pixel);

    s_panel = lcd_handles->panel_handle;
    s_io = lcd_handles->io_handle;
    s_width = lcd_config->lcd_width;
    s_height = lcd_config->lcd_height;
    /* RAW RGB565 pixels are little-endian; big-endian panel IO needs a swap. */
    s_swap_rgb565 = lcd_config->data_endian == LCD_RGB_DATA_ENDIAN_BIG;

    size_t framebuffer_bytes = (size_t)s_width * (size_t)s_height * sizeof(uint16_t);
    s_framebuffer = heap_caps_malloc(framebuffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_framebuffer == NULL) {
        s_framebuffer = heap_caps_malloc(framebuffer_bytes, MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "framebuffer allocation failed: %u bytes", (unsigned)framebuffer_bytes);
    memset(s_framebuffer, 0, framebuffer_bytes);

    size_t row_bytes = (size_t)s_width * sizeof(uint16_t);
    s_strip_bytes = DISPLAY_STRIP_BYTES;
    if (s_strip_bytes < row_bytes) {
        s_strip_bytes = row_bytes;
    }
    s_strip = heap_caps_malloc(s_strip_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (s_strip == NULL && s_strip_bytes > row_bytes) {
        s_strip_bytes = row_bytes;
        s_strip = heap_caps_malloc(s_strip_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_NO_MEM, TAG,
                        "DMA strip allocation failed: %u bytes", (unsigned)s_strip_bytes);

    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG, "flush semaphore allocation failed");

    esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = display_panel_color_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_io, &callbacks, NULL),
                        TAG, "register panel IO callbacks failed");

    /* Paint black once before the backlight comes up: the panel's GRAM is
     * undefined after reset, and lighting it first would flash noise. */
    err = display_panel_present();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "initial black frame failed: %s", esp_err_to_name(err));
    }

    /* board_init() normally claimed this already; "already initialized" is not
     * a failure, so resolve the handle unconditionally. */
    (void)esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS);
    void *backlight_handle = NULL;
    periph_ledc_config_t *backlight_config = NULL;
    if (esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, &backlight_handle) == ESP_OK &&
        backlight_handle != NULL &&
        esp_board_periph_get_config(ESP_BOARD_PERIPH_NAME_LEDC_BACKLIGHT, (void **)&backlight_config) == ESP_OK &&
        backlight_config != NULL) {
        s_backlight = (periph_ledc_handle_t *)backlight_handle;
        s_backlight_max_duty = (1u << (unsigned)backlight_config->duty_resolution) - 1u;
    }

    err = display_panel_backlight(100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "direct esp_lcd ready: %ux%u, strip=%u, swap=%d",
             s_width, s_height, (unsigned)s_strip_bytes, (int)s_swap_rgb565);
    return ESP_OK;
}

bool display_panel_is_open(void)
{
    return s_panel != NULL;
}

uint16_t *display_panel_framebuffer(void)
{
    return s_framebuffer;
}

uint16_t display_panel_width(void)
{
    return s_width;
}

uint16_t display_panel_height(void)
{
    return s_height;
}

esp_err_t display_panel_present(void)
{
    if (s_panel == NULL || s_framebuffer == NULL || s_strip == NULL || s_flush_done == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t row_bytes = (size_t)s_width * sizeof(uint16_t);
    size_t rows = s_strip_bytes / row_bytes;
    if (rows == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (rows > s_height) {
        rows = s_height;
    }

    for (uint16_t y = 0; y < s_height; y += (uint16_t)rows) {
        size_t chunk_rows = s_height - y;
        if (chunk_rows > rows) {
            chunk_rows = rows;
        }
        const uint16_t *src = s_framebuffer + (size_t)y * s_width;
        size_t pixels = chunk_rows * s_width;

        if (s_swap_rgb565) {
            uint16_t *dst = (uint16_t *)s_strip;
            for (size_t i = 0; i < pixels; i++) {
                dst[i] = __builtin_bswap16(src[i]);
            }
        } else {
            memcpy(s_strip, src, pixels * sizeof(uint16_t));
        }

        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_width, (uint16_t)(y + chunk_rows), s_strip);
        if (err != ESP_OK) {
            return err;
        }
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t display_panel_backlight(uint8_t percent)
{
    if (s_backlight == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (s_backlight_max_duty * (uint32_t)percent) / 100u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(s_backlight->speed_mode, s_backlight->channel, duty),
                        TAG, "set backlight duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(s_backlight->speed_mode, s_backlight->channel),
                        TAG, "update backlight duty failed");
    return ESP_OK;
}
