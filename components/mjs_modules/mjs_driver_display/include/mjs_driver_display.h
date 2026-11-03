#pragma once

#include "mjs_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the global `display` object.  The panel and its vendor init
 * sequence are owned by the board manager; this module draws into a PSRAM
 * framebuffer and display_panel stages/swaps the bytes for the panel IO. */
void mjs_module_display_init(JSContext *ctx);

#ifdef __cplusplus
}
#endif
