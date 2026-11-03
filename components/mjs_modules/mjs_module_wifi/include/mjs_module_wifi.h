#pragma once

#include <stdbool.h>
#include "mjs_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

void mjs_module_wifi_init(JSContext *ctx);
bool mjs_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
