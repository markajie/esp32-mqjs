/* ES8311 codec driver for the Movecall Moji 2.
 *
 * The reset/power-up register sequence and the volume/mute registers are
 * board hardware details.  The shared audio module owns I2S, decoding and the
 * JS API, and hands this driver a small I2C adapter declared in board.h. */

#include "../../board.h"

#include "esp_log.h"

#define TAG "es8311"

static const board_codec_bus_t *s_bus;

static esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    return s_bus->write_reg(s_bus->address, reg, value);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *value)
{
    return s_bus->read_reg(s_bus->address, reg, value);
}

static void reg_update(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    if (reg_read(reg, &current) == ESP_OK)
        reg_write(reg, (uint8_t)((current & ~mask) | (value & mask)));
}

static esp_err_t es8311_init(const board_codec_bus_t *bus)
{
    if (bus == NULL || bus->write_reg == NULL || bus->read_reg == NULL)
        return ESP_ERR_INVALID_ARG;
    s_bus = bus;

    uint8_t id1 = 0, id2 = 0;
    if (reg_read(0xFD, &id1) != ESP_OK || reg_read(0xFE, &id2) != ESP_OK) {
        ESP_LOGW(TAG, "not answering at 0x%02x", bus->address);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "id: %02x %02x", id1, id2);

    /* Reset and power-up sequence (Espressif esp_codec_dev reference). */
    reg_write(0x0D, 0xFA);
    reg_write(0x44, 0x08);
    reg_write(0x44, 0x08);
    reg_write(0x01, 0x30);
    reg_write(0x02, 0x00);
    reg_write(0x03, 0x10);
    reg_write(0x16, 0x24);
    reg_write(0x04, 0x10);
    reg_write(0x05, 0x00);
    reg_write(0x0B, 0x00);
    reg_write(0x0C, 0x00);
    reg_write(0x10, 0x1F);
    reg_write(0x11, 0x7F);
    reg_write(0x00, 0x80);
    reg_update(0x00, 0x40, 0x00);      /* slave mode */
    reg_write(0x01, 0x3F);              /* use MCLK pin, no inversion */
    reg_update(0x06, 0x20, 0x00);      /* SCLK not inverted */
    reg_write(0x13, 0x10);
    reg_write(0x1B, 0x0A);
    reg_write(0x1C, 0x6A);
    reg_write(0x44, 0x58);              /* internal reference */

    /* MCLK = 6.144 MHz, 24 kHz, 16-bit I2S slave. */
    reg_write(0x02, 0x00);
    reg_write(0x05, 0x00);
    reg_update(0x03, 0x7F, 0x10);
    reg_update(0x04, 0x7F, 0x10);
    reg_update(0x07, 0x3F, 0x00);
    reg_write(0x08, 0xFF);
    reg_update(0x06, 0x1F, 0x03);

    /* Normal I2S, 16-bit, DAC + ADC on. */
    reg_update(0x09, 0x1C, 0x0C);
    reg_update(0x0A, 0x1C, 0x0C);
    reg_write(0x17, 0xBF);
    reg_write(0x0E, 0x02);
    reg_write(0x12, 0x00);
    reg_write(0x14, 0x1A);
    reg_write(0x0D, 0x01);
    reg_write(0x15, 0x40);
    reg_write(0x37, 0x08);
    reg_write(0x45, 0x00);
    reg_write(0x31, 0x00);
    return ESP_OK;
}

static esp_err_t es8311_set_volume(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    return reg_write(0x32, (uint8_t)(percent * 255 / 100));
}

static esp_err_t es8311_mute(bool on)
{
    reg_update(0x31, 0x60, on ? 0x60 : 0x00);
    return ESP_OK;
}

static const board_codec_t s_codec = {
    .name = "ES8311",
    .init = es8311_init,
    .set_volume = es8311_set_volume,
    .mute = es8311_mute,
};

const board_codec_t *board_codec(void)
{
    return &s_codec;
}
