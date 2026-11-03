#include "mjs_module_ws.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "mjs_engine.h"
#include "mjs_module_wifi.h"

#define TAG "ws"
#define MAX_WS_PAYLOAD 8192

static esp_websocket_client_handle_t s_client;
static char *s_url;

static bool s_has_msg;
static JSGCRef s_msg_ref;
static bool s_has_conn;
static JSGCRef s_conn_ref;
static bool s_has_disc;
static JSGCRef s_disc_ref;

typedef struct {
    int kind; /* 0 data, 1 connected, 2 disconnected */
    char *data;
    int len;
} ws_evt_t;

static void deliver_evt(void *arg)
{
    ws_evt_t *evt = (ws_evt_t *)arg;
    JSContext *ctx = mjs_engine_ctx();
    if (evt->kind == 0 && s_has_msg) {
        JSValue s = JS_NewStringLen(ctx, evt->data ? evt->data : "", evt->len);
        mjs_call(ctx, s_msg_ref.val, 1, &s);
    } else if (evt->kind == 1 && s_has_conn) {
        mjs_call(ctx, s_conn_ref.val, 0, NULL);
    } else if (evt->kind == 2 && s_has_disc) {
        mjs_call(ctx, s_disc_ref.val, 0, NULL);
    }
    free(evt->data);
    free(evt);
}

static void post_evt(ws_evt_t *evt)
{
    mjs_engine_post(deliver_evt, evt);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    esp_websocket_event_data_t *e = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        {
            ws_evt_t *evt = calloc(1, sizeof(*evt));
            if (evt) { evt->kind = 1; post_evt(evt); }
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "disconnected");
        {
            ws_evt_t *evt = calloc(1, sizeof(*evt));
            if (evt) { evt->kind = 2; post_evt(evt); }
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!e || !e->data_ptr || e->data_len <= 0)
            break;
        if (e->op_code != 0x1 && e->op_code != 0x2)
            break;
        {
            int len = e->data_len > MAX_WS_PAYLOAD ? MAX_WS_PAYLOAD : e->data_len;
            ws_evt_t *evt = calloc(1, sizeof(*evt));
            if (!evt)
                break;
            evt->kind = 0;
            evt->data = malloc((size_t)len + 1);
            if (!evt->data) { free(evt); break; }
            memcpy(evt->data, e->data_ptr, (size_t)len);
            evt->data[len] = 0;
            evt->len = len;
            post_evt(evt);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "error");
        break;
    default:
        break;
    }
}

static void ws_cleanup(void)
{
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    free(s_url);
    s_url = NULL;
}

static const char *arg_str(JSContext *ctx, int argc, JSValue *argv, int idx,
                           JSCStringBuf *buf, size_t *len)
{
    *len = 0;
    if (idx >= argc || !JS_IsString(ctx, argv[idx]))
        return NULL;
    return JS_ToCStringLen(ctx, len, argv[idx], buf);
}

static JSValue js_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf ub;
    size_t ul = 0;
    const char *url = arg_str(ctx, argc, argv, 0, &ub, &ul);
    if (!url || ul == 0)
        return JS_ThrowTypeError(ctx, "ws.connect(url)");
    if (!mjs_wifi_is_connected())
        return mjs_throw(ctx, "ws: WiFi is not connected");

    ws_cleanup();
    s_url = strndup(url, ul);
    if (!s_url)
        return mjs_throw(ctx, "ws: out of memory");

    esp_websocket_client_config_t cfg = {0};
    cfg.uri = s_url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.reconnect_timeout_ms = 5000;
    cfg.network_timeout_ms = 10000;
    cfg.buffer_size = 4096;
    cfg.task_stack = 6144;
    cfg.ping_interval_sec = 20;
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client)
        return mjs_throw(ctx, "ws: init failed");
    esp_err_t err = esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    if (err == ESP_OK)
        err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ws_cleanup();
        return mjs_throw(ctx, "ws start: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "connecting %s", s_url);
    return JS_TRUE;
}

static JSValue js_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf sb;
    size_t sl = 0;
    const char *data = arg_str(ctx, argc, argv, 0, &sb, &sl);
    if (!data)
        return JS_ThrowTypeError(ctx, "ws.send(string)");
    if (!s_client || !esp_websocket_client_is_connected(s_client))
        return mjs_throw(ctx, "ws: not connected");
    int n = esp_websocket_client_send_text(s_client, data, (int)sl, pdMS_TO_TICKS(5000));
    if (n < 0)
        return mjs_throw(ctx, "ws send failed: %d", n);
    return JS_NewInt32(ctx, n);
}

static JSValue js_send_bin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf sb;
    size_t sl = 0;
    const char *data = arg_str(ctx, argc, argv, 0, &sb, &sl);
    if (!data)
        return JS_ThrowTypeError(ctx, "ws.sendBinary(string)");
    if (!s_client || !esp_websocket_client_is_connected(s_client))
        return mjs_throw(ctx, "ws: not connected");
    int n = esp_websocket_client_send_bin(s_client, data, (int)sl, pdMS_TO_TICKS(5000));
    if (n < 0)
        return mjs_throw(ctx, "ws send binary failed: %d", n);
    return JS_NewInt32(ctx, n);
}

static JSValue js_connected(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(s_client && esp_websocket_client_is_connected(s_client));
}

static JSValue js_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    ws_cleanup();
    return JS_TRUE;
}

static JSValue js_on(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque,
                     JSGCRef *ref, bool *has)
{
    (void)this_val; (void)opaque;
    if (*has) {
        JS_DeleteGCRef(ctx, ref);
        *has = false;
    }
    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
        return JS_UNDEFINED;
    if (!JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "ws callback must be a function");
    JSValue *slot = JS_AddGCRef(ctx, ref);
    *slot = argv[0];
    *has = true;
    return JS_UNDEFINED;
}

static JSValue js_on_message(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    return js_on(ctx, this_val, argc, argv, opaque, &s_msg_ref, &s_has_msg);
}
static JSValue js_on_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    return js_on(ctx, this_val, argc, argv, opaque, &s_conn_ref, &s_has_conn);
}
static JSValue js_on_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    return js_on(ctx, this_val, argc, argv, opaque, &s_disc_ref, &s_has_disc);
}

void mjs_module_ws_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "ws");
    mjs_module_add_fn(ctx, "ws", "connect", js_connect, NULL);
    mjs_module_add_fn(ctx, "ws", "send", js_send, NULL);
    mjs_module_add_fn(ctx, "ws", "sendBinary", js_send_bin, NULL);
    mjs_module_add_fn(ctx, "ws", "connected", js_connected, NULL);
    mjs_module_add_fn(ctx, "ws", "close", js_close, NULL);
    mjs_module_add_fn(ctx, "ws", "onMessage", js_on_message, NULL);
    mjs_module_add_fn(ctx, "ws", "onConnect", js_on_connect, NULL);
    mjs_module_add_fn(ctx, "ws", "onDisconnect", js_on_disconnect, NULL);
}
