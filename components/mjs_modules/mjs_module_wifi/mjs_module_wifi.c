#include "mjs_module_wifi.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "wifi"
#define BIT_CONNECTED   BIT0
#define BIT_FAIL        BIT1
#define BIT_GOT_IP      BIT2

static EventGroupHandle_t s_events;
static bool s_inited;
static bool s_connected;
static char s_ip[16];
static char s_ssid[33];
static int s_rssi;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = 0;
        xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_GOT_IP);
        xEventGroupSetBits(s_events, BIT_FAIL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_events, BIT_GOT_IP | BIT_CONNECTED);
        xEventGroupClearBits(s_events, BIT_FAIL);
    }
    (void)id;
}

static esp_err_t wifi_hw_init(void)
{
    if (s_inited)
        return ESP_OK;
    s_events = xEventGroupCreate();
    if (!s_events)
        return ESP_ERR_NO_MEM;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
        esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
#ifdef WIFI_BAND_MODE_AUTO
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);  /* ESP32-C5: 2.4G + 5G */
#endif
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    s_inited = true;
    return ESP_OK;
}

static JSValue js_init(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    esp_err_t err = wifi_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi init: %s", esp_err_to_name(err));
    return JS_TRUE;
}

static JSValue js_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "wifi.connect(ssid, password, timeoutMs)");
    JSCStringBuf sbuf, pbuf;
    size_t slen = 0, plen = 0;
    const char *ssid = JS_ToCStringLen(ctx, &slen, argv[0], &sbuf);
    const char *pass = argc > 1 && JS_IsString(ctx, argv[1])
                       ? JS_ToCStringLen(ctx, &plen, argv[1], &pbuf) : "";
    if (!ssid || slen == 0 || slen > 32)
        return JS_ThrowRangeError(ctx, "invalid SSID");
    if (!pass)
        pass = "";
    if (plen > 64)
        return JS_ThrowRangeError(ctx, "password too long");
    int timeout = 15000;
    if (argc > 2)
        JS_ToInt32(ctx, &timeout, argv[2]);

    esp_err_t err = wifi_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi init: %s", esp_err_to_name(err));

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, slen);
    memcpy(cfg.sta.password, pass, plen);
    cfg.sta.threshold.authmode = plen ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAIL | BIT_GOT_IP);
    s_connected = false;
    s_ip[0] = 0;
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_disconnect();
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK)
        err = esp_wifi_connect();
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi connect: %s", esp_err_to_name(err));

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_FAIL, pdFALSE,
                                           pdFALSE, pdMS_TO_TICKS(timeout));
    if (bits & BIT_GOT_IP) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            s_rssi = ap.rssi;
        return JS_TRUE;
    }
    esp_wifi_disconnect();
    return mjs_throw(ctx, "wifi connect timeout (%s)", ssid);
}

static JSValue js_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    esp_wifi_disconnect();
    s_connected = false;
    s_ip[0] = 0;
    return JS_UNDEFINED;
}

static JSValue js_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    JSGCRef o_ref;
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *o = JS_NewObject(ctx);
    if (!JS_IsException(*o)) {
        JS_SetPropertyStr(ctx, *o, "connected", JS_NewBool(s_connected));
        JS_SetPropertyStr(ctx, *o, "ssid", JS_NewString(ctx, s_ssid));
        JS_SetPropertyStr(ctx, *o, "ip", JS_NewString(ctx, s_ip));
        JS_SetPropertyStr(ctx, *o, "rssi", JS_NewInt32(ctx, s_rssi));
    }
    JSValue ret = *o;
    JS_PopGCRef(ctx, &o_ref);
    return ret;
}

static JSValue js_ip(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewString(ctx, s_ip);
}

static JSValue js_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return JS_NewString(ctx, buf);
}

static JSValue js_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    esp_err_t err = wifi_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi init: %s", esp_err_to_name(err));
    wifi_scan_config_t sc = { 0 };
    err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi scan: %s", esp_err_to_name(err));
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 40) n = 40;
    wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof(wifi_ap_record_t));
    if (!recs)
        return JS_ThrowInternalError(ctx, "out of memory");
    esp_wifi_scan_get_ap_records(&n, recs);

    JSGCRef a_ref;
    JSValue *a = JS_PushGCRef(ctx, &a_ref);
    *a = JS_NewArray(ctx, 0);
    for (int i = 0; i < n; i++) {
        JSGCRef o_ref;
        JSValue *o = JS_PushGCRef(ctx, &o_ref);
        *o = JS_NewObject(ctx);
        if (!JS_IsException(*o)) {
            JS_SetPropertyStr(ctx, *o, "ssid", JS_NewString(ctx, (const char *)recs[i].ssid));
            JS_SetPropertyStr(ctx, *o, "rssi", JS_NewInt32(ctx, recs[i].rssi));
            JS_SetPropertyStr(ctx, *o, "channel", JS_NewInt32(ctx, recs[i].primary));
            JS_SetPropertyStr(ctx, *o, "auth", JS_NewInt32(ctx, recs[i].authmode));
            JS_SetPropertyUint32(ctx, *a, (uint32_t)i, *o);
        }
        JS_PopGCRef(ctx, &o_ref);
    }
    free(recs);
    JSValue ret = *a;
    JS_PopGCRef(ctx, &a_ref);
    return ret;
}

static JSValue js_hostname(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc > 0 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf sbuf;
        size_t len = 0;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
        if (name) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif)
                esp_netif_set_hostname(netif, name);
        }
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const char *name = NULL;
    if (netif)
        esp_netif_get_hostname(netif, &name);
    return JS_NewString(ctx, name ? name : "esp32");
}

static JSValue js_start_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "wifi.startAP(ssid, password)");
    JSCStringBuf sbuf, pbuf;
    size_t slen = 0, plen = 0;
    const char *ssid = JS_ToCStringLen(ctx, &slen, argv[0], &sbuf);
    const char *pass = argc > 1 && JS_IsString(ctx, argv[1])
                       ? JS_ToCStringLen(ctx, &plen, argv[1], &pbuf) : "";
    if (!ssid || !pass || slen == 0 || slen > 32 || plen > 64)
        return JS_ThrowRangeError(ctx, "invalid AP credentials");

    esp_err_t err = wifi_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "wifi init: %s", esp_err_to_name(err));
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
        esp_netif_create_default_wifi_ap();
    wifi_config_t cfg = { 0 };
    memcpy(cfg.ap.ssid, ssid, slen);
    cfg.ap.ssid_len = (uint8_t)slen;
    memcpy(cfg.ap.password, pass, plen);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = plen ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err == ESP_OK)
        err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE)
        return mjs_throw(ctx, "wifi AP: %s", esp_err_to_name(err));
    return JS_TRUE;
}

bool mjs_wifi_is_connected(void)
{
    return s_connected;
}

void mjs_module_wifi_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "wifi");
    mjs_module_add_fn(ctx, "wifi", "init", js_init, NULL);
    mjs_module_add_fn(ctx, "wifi", "connect", js_connect, NULL);
    mjs_module_add_fn(ctx, "wifi", "disconnect", js_disconnect, NULL);
    mjs_module_add_fn(ctx, "wifi", "status", js_status, NULL);
    mjs_module_add_fn(ctx, "wifi", "ip", js_ip, NULL);
    mjs_module_add_fn(ctx, "wifi", "mac", js_mac, NULL);
    mjs_module_add_fn(ctx, "wifi", "scan", js_scan, NULL);
    mjs_module_add_fn(ctx, "wifi", "hostname", js_hostname, NULL);
    mjs_module_add_fn(ctx, "wifi", "startAP", js_start_ap, NULL);
}
