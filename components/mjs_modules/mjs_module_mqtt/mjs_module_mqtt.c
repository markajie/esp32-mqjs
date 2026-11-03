#include "mjs_module_mqtt.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mjs_engine.h"
#include "mjs_module_wifi.h"
#include "mqtt_client.h"

#define TAG "mqtt"
#define MAX_MQTT_PAYLOAD 8192

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static char *s_url;
static char *s_client_id;
static char *s_user;
static char *s_pass;

static bool s_has_msg;
static JSGCRef s_msg_ref;
static bool s_has_conn;
static JSGCRef s_conn_ref;
static bool s_has_disc;
static JSGCRef s_disc_ref;

typedef struct {
    int kind; /* 0 message, 1 connected, 2 disconnected */
    char *topic;
    char *data;
    int len;
} mqtt_evt_t;

static void deliver_evt(void *arg)
{
    mqtt_evt_t *evt = (mqtt_evt_t *)arg;
    JSContext *ctx = mjs_engine_ctx();
    if (evt->kind == 0 && s_has_msg) {
        JSValue args[2];
        args[0] = JS_NewString(ctx, evt->topic ? evt->topic : "");
        args[1] = JS_NewStringLen(ctx, evt->data ? evt->data : "", evt->len);
        mjs_call(ctx, s_msg_ref.val, 2, args);
    } else if (evt->kind == 1 && s_has_conn) {
        mjs_call(ctx, s_conn_ref.val, 0, NULL);
    } else if (evt->kind == 2 && s_has_disc) {
        mjs_call(ctx, s_disc_ref.val, 0, NULL);
    }
    free(evt->topic);
    free(evt->data);
    free(evt);
}

static void post_evt(mqtt_evt_t *evt)
{
    mjs_engine_post(deliver_evt, evt);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)event_data;
    if (!e)
        return;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected");
        {
            mqtt_evt_t *evt = calloc(1, sizeof(*evt));
            if (evt) { evt->kind = 1; post_evt(evt); }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        {
            mqtt_evt_t *evt = calloc(1, sizeof(*evt));
            if (evt) { evt->kind = 2; post_evt(evt); }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "error type %d", e->error_handle ? e->error_handle->error_type : -1);
        break;
    case MQTT_EVENT_DATA:
        if (!e->data || e->data_len <= 0)
            break;
        {
            int len = e->data_len > MAX_MQTT_PAYLOAD ? MAX_MQTT_PAYLOAD : e->data_len;
            mqtt_evt_t *evt = calloc(1, sizeof(*evt));
            if (!evt)
                break;
            evt->kind = 0;
            evt->topic = strndup(e->topic ? e->topic : "", e->topic_len);
            evt->data = malloc((size_t)len + 1);
            if (!evt->data) {
                free(evt->topic); free(evt); break;
            }
            memcpy(evt->data, e->data, (size_t)len);
            evt->data[len] = 0;
            evt->len = len;
            post_evt(evt);
        }
        break;
    default:
        break;
    }
}

static void mqtt_cleanup(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    free(s_url); s_url = NULL;
    free(s_client_id); s_client_id = NULL;
    free(s_user); s_user = NULL;
    free(s_pass); s_pass = NULL;
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
    JSCStringBuf ub, cb, nb, pb;
    size_t ul = 0, cl = 0, nl = 0, pl = 0;
    const char *url = arg_str(ctx, argc, argv, 0, &ub, &ul);
    if (!url || ul == 0)
        return JS_ThrowTypeError(ctx, "mqtt.connect(url, clientId?, user?, pass?)");
    if (!mjs_wifi_is_connected())
        return mjs_throw(ctx, "mqtt: WiFi is not connected");
    const char *cid = arg_str(ctx, argc, argv, 1, &cb, &cl);
    const char *user = arg_str(ctx, argc, argv, 2, &nb, &nl);
    const char *pass = arg_str(ctx, argc, argv, 3, &pb, &pl);

    mqtt_cleanup();
    s_url = strndup(url, ul);
    s_client_id = cid ? strndup(cid, cl) : NULL;
    s_user = user ? strndup(user, nl) : NULL;
    s_pass = pass ? strndup(pass, pl) : NULL;
    if (!s_url)
        return mjs_throw(ctx, "mqtt: out of memory");

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = s_url;
    cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.credentials.client_id = s_client_id;
    cfg.credentials.username = s_user;
    cfg.credentials.authentication.password = s_pass;
    cfg.session.keepalive = 30;
    cfg.network.reconnect_timeout_ms = 5000;
    cfg.network.timeout_ms = 10000;
    cfg.task.stack_size = 6144;
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client)
        return mjs_throw(ctx, "mqtt: init failed");
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    if (err == ESP_OK)
        err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        mqtt_cleanup();
        return mjs_throw(ctx, "mqtt start: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "connecting %s", s_url);
    return JS_TRUE;
}

static JSValue js_publish(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf tb, pb;
    size_t tl = 0, pl = 0;
    const char *topic = arg_str(ctx, argc, argv, 0, &tb, &tl);
    const char *payload = arg_str(ctx, argc, argv, 1, &pb, &pl);
    if (!topic || tl == 0)
        return JS_ThrowTypeError(ctx, "mqtt.publish(topic, payload, qos?, retain?)");
    if (!s_client)
        return mjs_throw(ctx, "mqtt: not connected");
    int qos = 0, retain = 0;
    if (argc > 2) JS_ToInt32(ctx, &qos, argv[2]);
    if (argc > 3) JS_ToInt32(ctx, &retain, argv[3]);
    int id = esp_mqtt_client_publish(s_client, topic, payload ? payload : "", (int)pl, qos, retain);
    if (id < 0)
        return mjs_throw(ctx, "mqtt publish failed: %d", id);
    return JS_NewInt32(ctx, id);
}

static JSValue js_subscribe(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    JSCStringBuf tb;
    size_t tl = 0;
    const char *topic = arg_str(ctx, argc, argv, 0, &tb, &tl);
    if (!topic || tl == 0)
        return JS_ThrowTypeError(ctx, "mqtt.subscribe(topic, qos?)");
    if (!s_client)
        return mjs_throw(ctx, "mqtt: not connected");
    int qos = 0;
    if (argc > 1) JS_ToInt32(ctx, &qos, argv[1]);
    int id = esp_mqtt_client_subscribe_single(s_client, topic, qos);
    if (id < 0)
        return mjs_throw(ctx, "mqtt subscribe failed: %d", id);
    return JS_NewInt32(ctx, id);
}

static JSValue js_connected(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(s_connected);
}

static JSValue js_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    mqtt_cleanup();
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
        return JS_ThrowTypeError(ctx, "mqtt callback must be a function");
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

void mjs_module_mqtt_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "mqtt");
    mjs_module_add_fn(ctx, "mqtt", "connect", js_connect, NULL);
    mjs_module_add_fn(ctx, "mqtt", "publish", js_publish, NULL);
    mjs_module_add_fn(ctx, "mqtt", "subscribe", js_subscribe, NULL);
    mjs_module_add_fn(ctx, "mqtt", "connected", js_connected, NULL);
    mjs_module_add_fn(ctx, "mqtt", "disconnect", js_disconnect, NULL);
    mjs_module_add_fn(ctx, "mqtt", "onMessage", js_on_message, NULL);
    mjs_module_add_fn(ctx, "mqtt", "onConnect", js_on_connect, NULL);
    mjs_module_add_fn(ctx, "mqtt", "onDisconnect", js_on_disconnect, NULL);
}
