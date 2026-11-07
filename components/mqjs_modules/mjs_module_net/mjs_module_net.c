#include "mjs_module_net.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#define TAG "net"
#define MAX_BODY (128 * 1024)  /* must fit in the JS heap */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool too_big;
} body_t;

static esp_err_t http_event(esp_http_client_event_t *ev)
{
    body_t *b = (body_t *)ev->user_data;
    if (ev->event_id == HTTP_EVENT_ON_DATA && b) {
        if (b->len + ev->data_len > MAX_BODY) {
            b->too_big = true;
            return ESP_FAIL;
        }
        if (b->len + ev->data_len + 1 > b->cap) {
            size_t cap = b->cap ? b->cap : 4096;
            while (cap < b->len + ev->data_len + 1)
                cap *= 2;
            char *p = realloc(b->data, cap);
            if (!p)
                return ESP_ERR_NO_MEM;
            b->data = p;
            b->cap = cap;
        }
        memcpy(b->data + b->len, ev->data, ev->data_len);
        b->len += ev->data_len;
        b->data[b->len] = 0;
    }
    return ESP_OK;
}

static JSValue make_response(JSContext *ctx, int status, body_t *b)
{
    JSGCRef o_ref;
    JSValue *o = JS_PushGCRef(ctx, &o_ref);
    *o = JS_NewObject(ctx);
    if (!JS_IsException(*o)) {
        JS_SetPropertyStr(ctx, *o, "status", JS_NewInt32(ctx, status));
        JS_SetPropertyStr(ctx, *o, "ok", JS_NewBool(status >= 200 && status < 300));
        JS_SetPropertyStr(ctx, *o, "body",
                          JS_NewStringLen(ctx, b->data ? b->data : "", b->len));
        if (b->too_big)
            JS_SetPropertyStr(ctx, *o, "truncated", JS_TRUE);
    }
    JSValue ret = *o;
    JS_PopGCRef(ctx, &o_ref);
    return ret;
}

static JSValue do_request(JSContext *ctx, const char *url, const char *method,
                          const char *body, const char *content_type,
                          const char *headers, int timeout_ms)
{
    body_t resp = { 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
        .event_handler = http_event,
        .user_data = &resp,
        .buffer_size = 8192,
        .buffer_size_tx = 4096,
    };
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp.data);
        return JS_ThrowInternalError(ctx, "http client init failed");
    }

    if (method) {
        if (!strcasecmp(method, "POST"))
            esp_http_client_set_method(client, HTTP_METHOD_POST);
        else if (!strcasecmp(method, "PUT"))
            esp_http_client_set_method(client, HTTP_METHOD_PUT);
        else if (!strcasecmp(method, "DELETE"))
            esp_http_client_set_method(client, HTTP_METHOD_DELETE);
        else if (!strcasecmp(method, "PATCH"))
            esp_http_client_set_method(client, HTTP_METHOD_PATCH);
        else if (!strcasecmp(method, "HEAD"))
            esp_http_client_set_method(client, HTTP_METHOD_HEAD);
        else
            esp_http_client_set_method(client, HTTP_METHOD_GET);
    }
    if (content_type)
        esp_http_client_set_header(client, "Content-Type", content_type);
    if (headers && *headers) {
        char *copy = strdup(headers);
        char *save = NULL;
        for (char *line = strtok_r(copy, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            char *colon = strchr(line, ':');
            if (!colon)
                continue;
            *colon = 0;
            char *val = colon + 1;
            while (*val == ' ')
                val++;
            esp_http_client_set_header(client, line, val);
        }
        free(copy);
    }
    if (body)
        esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (resp.too_big) {
        free(resp.data);
        return JS_ThrowRangeError(ctx, "response larger than %d bytes", MAX_BODY);
    }
    if (err != ESP_OK) {
        free(resp.data);
        return mjs_throw(ctx, "http %s: %s", url, esp_err_to_name(err));
    }
    JSValue ret = make_response(ctx, status, &resp);
    free(resp.data);
    return ret;
}

static JSValue js_request(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "http.request({url, method, body, headers, contentType, timeout})");
    JSValue opts = argv[0];
    JSCStringBuf ubuf, mbuf, bbuf, cbuf, hbuf;
    size_t ulen = 0, mlen = 0, blen = 0, clen = 0, hlen = 0;
    JSValue vurl = JS_GetPropertyStr(ctx, opts, "url");
    const char *url = JS_IsString(ctx, vurl) ? JS_ToCStringLen(ctx, &ulen, vurl, &ubuf) : NULL;
    if (!url)
        return JS_ThrowTypeError(ctx, "url is required");

    JSValue vmethod = JS_GetPropertyStr(ctx, opts, "method");
    const char *method = JS_IsString(ctx, vmethod) ? JS_ToCStringLen(ctx, &mlen, vmethod, &mbuf) : "GET";
    JSValue vbody = JS_GetPropertyStr(ctx, opts, "body");
    const char *body = JS_IsString(ctx, vbody) ? JS_ToCStringLen(ctx, &blen, vbody, &bbuf) : NULL;
    JSValue vct = JS_GetPropertyStr(ctx, opts, "contentType");
    const char *ct = JS_IsString(ctx, vct) ? JS_ToCStringLen(ctx, &clen, vct, &cbuf) : NULL;
    JSValue vh = JS_GetPropertyStr(ctx, opts, "headers");
    const char *headers = JS_IsString(ctx, vh) ? JS_ToCStringLen(ctx, &hlen, vh, &hbuf) : NULL;
    int timeout = 15000;
    JSValue vt = JS_GetPropertyStr(ctx, opts, "timeout");
    if (JS_IsNumber(ctx, vt))
        JS_ToInt32(ctx, &timeout, vt);
    (void)ulen; (void)mlen; (void)blen; (void)clen; (void)hlen;
    return do_request(ctx, url, method, body, ct, headers, timeout);
}

static JSValue js_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "http.get(url, headers?)");
    JSCStringBuf ubuf, hbuf;
    size_t ulen = 0, hlen = 0;
    const char *url = JS_ToCStringLen(ctx, &ulen, argv[0], &ubuf);
    const char *headers = argc > 1 && JS_IsString(ctx, argv[1])
                          ? JS_ToCStringLen(ctx, &hlen, argv[1], &hbuf) : NULL;
    int timeout = argc > 2 ? 15000 : 15000;
    if (argc > 2)
        JS_ToInt32(ctx, &timeout, argv[2]);
    return do_request(ctx, url, "GET", NULL, NULL, headers, timeout);
}

static JSValue js_post(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 2 || !JS_IsString(ctx, argv[0]) || !JS_IsString(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "http.post(url, body, contentType?, headers?)");
    JSCStringBuf ubuf, bbuf, cbuf, hbuf;
    size_t ulen = 0, blen = 0, clen = 0, hlen = 0;
    const char *url = JS_ToCStringLen(ctx, &ulen, argv[0], &ubuf);
    const char *body = JS_ToCStringLen(ctx, &blen, argv[1], &bbuf);
    const char *ct = argc > 2 && JS_IsString(ctx, argv[2])
                     ? JS_ToCStringLen(ctx, &clen, argv[2], &cbuf) : "text/plain";
    const char *headers = argc > 3 && JS_IsString(ctx, argv[3])
                          ? JS_ToCStringLen(ctx, &hlen, argv[3], &hbuf) : NULL;
    return do_request(ctx, url, "POST", body, ct, headers, 15000);
}

static JSValue js_resolve(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "net.resolve(host)");
    JSCStringBuf sbuf;
    size_t len = 0;
    const char *host = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
        return mjs_throw(ctx, "cannot resolve %s", host);
    JSGCRef a_ref;
    JSValue *a = JS_PushGCRef(ctx, &a_ref);
    *a = JS_NewArray(ctx, 0);
    int n = 0;
    for (struct addrinfo *p = res; p && n < 16; p = p->ai_next) {
        char ip[46];
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)p->ai_addr)->sin_addr,
                      ip, sizeof(ip)))
            JS_SetPropertyUint32(ctx, *a, (uint32_t)n++, JS_NewString(ctx, ip));
    }
    freeaddrinfo(res);
    JSValue ret = *a;
    JS_PopGCRef(ctx, &a_ref);
    return ret;
}

static JSValue js_sntp(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    const char *server = "pool.ntp.org";
    JSCStringBuf sbuf;
    size_t len = 0;
    if (argc > 0 && JS_IsString(ctx, argv[0]))
        server = JS_ToCStringLen(ctx, &len, argv[0], &sbuf);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    for (int i = 0; i < 100; i++) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
            return JS_TRUE;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return JS_FALSE;
}

void mjs_module_net_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "net");
    mjs_module_add_fn(ctx, "net", "request", js_request, NULL);
    mjs_module_add_fn(ctx, "net", "get", js_get, NULL);
    mjs_module_add_fn(ctx, "net", "post", js_post, NULL);
    mjs_module_add_fn(ctx, "net", "resolve", js_resolve, NULL);
    mjs_module_add_fn(ctx, "net", "sntp", js_sntp, NULL);
}
