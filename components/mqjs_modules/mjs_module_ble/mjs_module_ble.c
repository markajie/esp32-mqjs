#include "mjs_module_ble.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "ble"
#define MAX_PAYLOAD 512

/* 6e400001-b5a3-f393-e0a9-e50e24dcca9e (Nordic UART style) */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0xe5, 0x0e, 0xe0, 0xa9,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0xe5, 0x0e, 0xe0, 0xa9,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0xe5, 0x0e, 0xe0, 0xa9,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static bool s_inited;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_rx_handle;
static uint16_t s_tx_handle;
static uint8_t s_rx_buf[MAX_PAYLOAD];
static uint8_t s_tx_buf[MAX_PAYLOAD];
static uint16_t s_tx_len;
static uint8_t s_own_addr_type;
static char s_name[32] = "ESP32-C5";
static JSGCRef s_cb_ref;
static bool s_has_cb;

static void advertise(void);

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_chr_def s_chars[] = {
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &s_rx_handle,
    },
    {
        .uuid = &s_tx_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_handle,
    },
    { 0 },
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = s_chars,
    },
    { 0 },
};

typedef struct {
    uint16_t len;
    uint8_t data[1];
} ble_msg_t;

static void deliver_msg(void *arg)
{
    ble_msg_t *m = (ble_msg_t *)arg;
    if (s_has_cb) {
        JSContext *ctx = mjs_engine_ctx();
        JSValue s = JS_NewStringLen(ctx, (const char *)m->data, m->len);
        mjs_call(ctx, s_cb_ref.val, 1, &s);
    }
    free(m);
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > MAX_PAYLOAD)
            len = MAX_PAYLOAD;
        if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, sizeof(s_rx_buf), &len) != 0)
            return BLE_ATT_ERR_UNLIKELY;
        ble_msg_t *m = malloc(sizeof(ble_msg_t) + len);
        if (!m)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        m->len = len;
        memcpy(m->data, s_rx_buf, len);
        mjs_engine_post(deliver_msg, m);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_tx_buf, s_tx_len) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
        } else {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "disconnected");
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    default:
        return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (uint8_t *)s_name;
    rsp.name_len = strlen(s_name);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY)
        ESP_LOGE(TAG, "adv start: %d", rc);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0)
        return;
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0)
        return;
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset: %d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_hw_init(void)
{
    if (s_inited)
        return ESP_OK;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
        return err;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_att_set_preferred_mtu(MAX_PAYLOAD);

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0)
        rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0)
        return ESP_FAIL;
    ble_svc_gap_device_name_set(s_name);
    nimble_port_freertos_init(host_task);
    s_inited = true;
    return ESP_OK;
}

/* ---- JS bindings ------------------------------------------------------- */

static JSValue js_init(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc > 0 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf sbuf;
        size_t len = 0;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
        if (name && len > 0) {
            size_t n = len < sizeof(s_name) - 1 ? len : sizeof(s_name) - 1;
            memcpy(s_name, name, n);
            s_name[n] = 0;
        }
    }
    esp_err_t err = ble_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "ble init: %s", esp_err_to_name(err));
    if (s_inited)
        ble_svc_gap_device_name_set(s_name);
    return JS_TRUE;
}

static JSValue js_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc > 0 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf sbuf;
        size_t len = 0;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
        if (name) {
            size_t n = len < sizeof(s_name) - 1 ? len : sizeof(s_name) - 1;
            memcpy(s_name, name, n);
            s_name[n] = 0;
            if (s_inited)
                ble_svc_gap_device_name_set(s_name);
        }
    }
    return JS_NewString(ctx, s_name);
}

static JSValue js_address(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    uint8_t addr[6] = { 0 };
    if (ble_hs_id_copy_addr(s_own_addr_type, addr, NULL) != 0)
        return JS_NewString(ctx, "");
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    return JS_NewString(ctx, buf);
}

static JSValue js_connected(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(s_conn != BLE_HS_CONN_HANDLE_NONE);
}

static JSValue js_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "ble.send(string)");
    if (s_conn == BLE_HS_CONN_HANDLE_NONE)
        return mjs_throw(ctx, "ble not connected");
    JSCStringBuf sbuf;
    size_t len = 0;
    const char *s = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
    if (!s || len == 0)
        return JS_NewInt32(ctx, 0);
    if (len > MAX_PAYLOAD)
        len = MAX_PAYLOAD;
    if (len > sizeof(s_tx_buf))
        len = sizeof(s_tx_buf);
    memcpy(s_tx_buf, s, len);
    s_tx_len = (uint16_t)len;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(s, len);
    if (!om)
        return mjs_throw(ctx, "ble out of memory");
    int rc = ble_gatts_notify_custom(s_conn, s_tx_handle, om);
    if (rc != 0)
        return mjs_throw(ctx, "ble notify: %d", rc);
    return JS_NewInt32(ctx, (int)len);
}

static JSValue js_on_receive(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "ble.onReceive(function)");
    if (s_has_cb) {
        JS_DeleteGCRef(ctx, &s_cb_ref);
        s_has_cb = false;
    }
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
        return JS_UNDEFINED;
    if (!JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "ble.onReceive needs a function");
    JSValue *slot = JS_AddGCRef(ctx, &s_cb_ref);
    *slot = argv[0];
    s_has_cb = true;
    return JS_UNDEFINED;
}

static JSValue js_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    if (s_inited)
        ble_gap_adv_stop();
    return JS_UNDEFINED;
}

void mjs_module_ble_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "ble");
    mjs_module_add_fn(ctx, "ble", "init", js_init, NULL);
    mjs_module_add_fn(ctx, "ble", "name", js_name, NULL);
    mjs_module_add_fn(ctx, "ble", "address", js_address, NULL);
    mjs_module_add_fn(ctx, "ble", "connected", js_connected, NULL);
    mjs_module_add_fn(ctx, "ble", "send", js_send, NULL);
    mjs_module_add_fn(ctx, "ble", "onReceive", js_on_receive, NULL);
    mjs_module_add_fn(ctx, "ble", "stop", js_stop, NULL);
}
