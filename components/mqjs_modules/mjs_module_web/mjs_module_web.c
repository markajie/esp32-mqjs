#include "mjs_module_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mjs_engine.h"

#define TAG "web"
#define MAX_JS (256 * 1024)

static httpd_handle_t s_server;
static esp_timer_handle_t s_reboot_timer;

static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(void)
{
    if (!s_reboot_timer) {
        const esp_timer_create_args_t args = {
            .callback = reboot_cb,
            .name = "web_reboot",
        };
        if (esp_timer_create(&args, &s_reboot_timer) != ESP_OK)
            return;
    }
    esp_timer_start_once(s_reboot_timer, 1000 * 1000);
}

static uint8_t *read_script(size_t *len)
{
    *len = 0;
    uint8_t *buf = mjs_read_file("/spiffs/user.js", len);
    if (!buf || *len == 0) {
        free(buf);
        buf = mjs_read_file("/spiffs/main.js", len);
    }
    return buf;
}

static char *html_escape(const char *s, size_t len)
{
    if (!s)
        len = 0;
    if (len > MAX_JS)
        len = MAX_JS;
    char *out = malloc(len * 6 + 1);
    if (!out)
        return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
        case '&': memcpy(out + j, "&amp;", 5); j += 5; break;
        case '<': memcpy(out + j, "&lt;", 4); j += 4; break;
        case '>': memcpy(out + j, "&gt;", 4); j += 4; break;
        case '"': memcpy(out + j, "&quot;", 6); j += 6; break;
        case '\'': memcpy(out + j, "&#39;", 5); j += 5; break;
        default: out[j++] = s[i]; break;
        }
    }
    out[j] = 0;
    return out;
}

static const char HTML_HEAD[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>MQuickJS</title><style>"
    "body{font-family:system-ui,sans-serif;margin:1.5rem;background:#111;color:#eee}"
    "textarea{width:100%;height:62vh;background:#1b1b1b;color:#d4d4d4;"
    "font:13px/1.4 monospace;border:1px solid #444;padding:.6rem;box-sizing:border-box}"
    "button{padding:.6rem 1rem;margin:.4rem .4rem 0 0;font-size:1rem;cursor:pointer}"
    "#s{white-space:pre-wrap;color:#8f8}</style></head><body>"
    "<h2>MQuickJS <small>/spiffs/user.js</small></h2>"
    "<p>Save writes user.js and reboots. Delete returns to the built-in main.js.</p>"
    "<textarea id=\"c\">";

static const char HTML_TAIL[] =
    "</textarea><br><button onclick=\"save()\">Save &amp; reboot</button>"
    "<button onclick=\"reset()\">Delete user.js &amp; reboot</button>"
    "<pre id=\"s\"></pre><script>"
    "const t=document.getElementById('c'),s=document.getElementById('s');"
    "async function save(){const r=await fetch('/save',{method:'POST',body:t.value});s.textContent=await r.text();}"
    "async function reset(){if(!confirm('Delete /spiffs/user.js?'))return;"
    "const r=await fetch('/reset',{method:'POST'});s.textContent=await r.text();}"
    "</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    size_t len = 0;
    uint8_t *code = read_script(&len);
    char *esc = html_escape((const char *)code, len);
    free(code);
    if (esc == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    httpd_resp_sendstr_chunk(req, esc);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    free(esc);
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > MAX_JS) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty or too large");
        return ESP_FAIL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (n <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "receive failed");
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    body[got] = 0;
    if (!mjs_write_file("/spiffs/user.js", body, got)) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "saved /spiffs/user.js (%u bytes)", (unsigned)got);
    free(body);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "saved; rebooting...");
    schedule_reboot();
    return ESP_OK;
}

static esp_err_t reset_post(httpd_req_t *req)
{
    remove("/spiffs/user.js");
    ESP_LOGI(TAG, "removed /spiffs/user.js");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "user.js deleted; rebooting...");
    schedule_reboot();
    return ESP_OK;
}


static esp_err_t web_hw_start(int port)
{
    if (s_server)
        return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }
    static const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/save", .method = HTTP_POST, .handler = save_post },
        { .uri = "/reset", .method = HTTP_POST, .handler = reset_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s: %s", uris[i].uri, esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "listening on port %d", port);
    return ESP_OK;
}

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int v = def;
    if (idx < argc)
        JS_ToInt32(ctx, &v, argv[idx]);
    return v;
}

static JSValue js_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int port = arg_int(ctx, argc, argv, 0, 80);
    if (port < 1 || port > 65535)
        return JS_ThrowRangeError(ctx, "web.start(port)");
    esp_err_t err = web_hw_start(port);
    if (err != ESP_OK)
        return mjs_throw(ctx, "web start: %s", esp_err_to_name(err));
    return JS_NewInt32(ctx, port);
}

static JSValue js_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return JS_TRUE;
}

static JSValue js_ready(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(s_server != NULL);
}

void mjs_module_web_init(JSContext *ctx)
{
    mjs_module_begin(ctx, "web");
    mjs_module_add_fn(ctx, "web", "start", js_start, NULL);
    mjs_module_add_fn(ctx, "web", "stop", js_stop, NULL);
    mjs_module_add_fn(ctx, "web", "ready", js_ready, NULL);
}
