/* ST7789 panel factory for the ZhengChen 1.54" TFT WiFi board.
 *
 * esp_board_manager creates the SPI panel IO from board_devices.yaml and then
 * calls this board-local entry point to build the actual panel driver, so the
 * chip-specific part never leaks into shared code. */

#include <string.h>

/* ESP-IDF 6.1 ships the ST7789 driver inside the esp_lcd component, so the
 * board needs no extra managed component for it. */
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));

    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE("setup_device", "New ST7789 panel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}
