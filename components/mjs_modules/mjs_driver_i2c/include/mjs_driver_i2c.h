#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mjs_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared I2C master bus used by JS and by the audio codec driver. */
esp_err_t mjs_i2c_init(int sda, int scl, uint32_t hz);
bool mjs_i2c_ready(void);
esp_err_t mjs_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t mjs_i2c_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t mjs_i2c_write_read(uint8_t addr, const uint8_t *wbuf, size_t wlen,
                             uint8_t *rbuf, size_t rlen);
esp_err_t mjs_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
esp_err_t mjs_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val);
int mjs_i2c_probe(uint8_t addr);

void mjs_driver_i2c_init(JSContext *ctx);

#ifdef __cplusplus
}
#endif
