#pragma once

#include <stdint.h>
#include "mjs_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int mclk;
    int ws;
    int bclk;
    int din;
    int dout;
    int pa;
    int i2c_sda;
    int i2c_scl;
    int codec_addr;
} mjs_audio_pins_t;

void mjs_module_audio_init(JSContext *ctx, const mjs_audio_pins_t *pins);

#ifdef __cplusplus
}
#endif
