#include "mjs_module_audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mjs_driver_i2c.h"
#include "mjs_engine.h"
#include "esp_audio_simple_dec.h"
#include "esp_opus_dec.h"
#include "simple_dec/impl/esp_ogg_dec.h"

#define TAG "audio"
#define SAMPLE_RATE 24000
#define MAX_SAMPLES (SAMPLE_RATE * 10)

static mjs_audio_pins_t s_pins;
static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static bool s_ready;
static int s_volume = 70;

/* ---- ES8311 register helpers ------------------------------------------ */

static esp_err_t codec_write(uint8_t reg, uint8_t val)
{
    return mjs_i2c_write_reg((uint8_t)s_pins.codec_addr, reg, val);
}

static esp_err_t codec_read(uint8_t reg, uint8_t *val)
{
    return mjs_i2c_read_reg((uint8_t)s_pins.codec_addr, reg, val);
}

static void codec_update(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t v = 0;
    if (codec_read(reg, &v) != ESP_OK)
        return;
    v = (uint8_t)((v & ~mask) | (val & mask));
    codec_write(reg, v);
}

static esp_err_t es8311_init(void)
{
    esp_err_t err = ESP_OK;
    uint8_t v = 0;

    /* Make sure the shared bus exists (main usually does this already). */
    if (!mjs_i2c_ready()) {
        err = mjs_i2c_init(s_pins.i2c_sda, s_pins.i2c_scl, 100000);
        if (err != ESP_OK)
            return err;
    }

    uint8_t id1 = 0, id2 = 0;
    if (codec_read(0xFD, &id1) != ESP_OK || codec_read(0xFE, &id2) != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 not answering at 0x%02x", s_pins.codec_addr);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "ES8311 id: %02x %02x", id1, id2);

    /* Reset and power-up sequence (Espressif esp_codec_dev reference). */
    codec_write(0x0D, 0xFA);
    codec_write(0x44, 0x08);
    codec_write(0x44, 0x08);
    codec_write(0x01, 0x30);
    codec_write(0x02, 0x00);
    codec_write(0x03, 0x10);
    codec_write(0x16, 0x24);
    codec_write(0x04, 0x10);
    codec_write(0x05, 0x00);
    codec_write(0x0B, 0x00);
    codec_write(0x0C, 0x00);
    codec_write(0x10, 0x1F);
    codec_write(0x11, 0x7F);
    codec_write(0x00, 0x80);
    codec_update(0x00, 0x40, 0x00);      /* slave mode */
    codec_write(0x01, 0x3F);              /* use MCLK pin, no inversion */
    codec_update(0x06, 0x20, 0x00);      /* SCLK not inverted */
    codec_write(0x13, 0x10);
    codec_write(0x1B, 0x0A);
    codec_write(0x1C, 0x6A);
    codec_write(0x44, 0x58);              /* internal reference */

    /* MCLK = 6.144 MHz, 24 kHz, 16-bit I2S slave. */
    codec_write(0x02, 0x00);
    codec_write(0x05, 0x00);
    codec_update(0x03, 0x7F, 0x10);
    codec_update(0x04, 0x7F, 0x10);
    codec_update(0x07, 0x3F, 0x00);
    codec_write(0x08, 0xFF);
    codec_update(0x06, 0x1F, 0x03);

    /* Normal I2S, 16-bit, DAC + ADC on. */
    codec_update(0x09, 0x1C, 0x0C);
    codec_update(0x0A, 0x1C, 0x0C);
    codec_write(0x17, 0xBF);
    codec_write(0x0E, 0x02);
    codec_write(0x12, 0x00);
    codec_write(0x14, 0x1A);
    codec_write(0x0D, 0x01);
    codec_write(0x15, 0x40);
    codec_write(0x37, 0x08);
    codec_write(0x45, 0x00);
    codec_write(0x31, 0x00);
    codec_write(0x32, (uint8_t)(s_volume * 255 / 100));
    return ESP_OK;
}

static esp_err_t audio_hw_init(void)
{
    if (s_ready)
        return ESP_OK;

    esp_err_t err = mjs_i2c_init(s_pins.i2c_sda, s_pins.i2c_scl, 100000);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "i2c init: %s", esp_err_to_name(err));

    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << s_pins.pa,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pa);
    gpio_set_level(s_pins.pa, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    err = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (err != ESP_OK)
        return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = s_pins.mclk,
            .bclk = s_pins.bclk,
            .ws = s_pins.ws,
            .dout = s_pins.dout,
            .din = s_pins.din,
            .invert_flags = { 0 },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err == ESP_OK)
        err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err == ESP_OK)
        err = i2s_channel_enable(s_tx);
    if (err == ESP_OK)
        err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = es8311_init();
    gpio_set_level(s_pins.pa, 1);
    s_ready = true;
    ESP_LOGI(TAG, "audio ready (%d Hz, %s)", SAMPLE_RATE,
             err == ESP_OK ? "ES8311" : "i2s only");
    return ESP_OK;
}

/* ---- sample helpers ---------------------------------------------------- */

static int16_t *get_samples(JSContext *ctx, JSValue val, int *count, size_t *bytes,
                            bool *from_array)
{
    *count = 0;
    *bytes = 0;
    *from_array = false;

    if (JS_IsString(ctx, val)) {
        JSCStringBuf sbuf;
        size_t len = 0;
        const char *s = JS_ToCStringLen(ctx, &len, val, &sbuf);
        if (!s)
            return NULL;
        uint8_t *buf = malloc(len + 1);
        if (!buf)
            return NULL;
        memcpy(buf, s, len);
        buf[len] = 0;
        *bytes = len;
        *count = (int)(len / 2);
        return (int16_t *)buf;
    }

    JSValue lenv = JS_GetPropertyStr(ctx, val, "length");
    int n = 0;
    if (JS_ToInt32(ctx, &n, lenv) || n <= 0 || n > MAX_SAMPLES)
        return NULL;
    int16_t *buf = malloc((size_t)n * sizeof(int16_t));
    if (!buf)
        return NULL;
    for (int i = 0; i < n; i++) {
        int v = 0;
        JSValue item = JS_GetPropertyUint32(ctx, val, (uint32_t)i);
        if (JS_ToInt32(ctx, &v, item)) {
            free(buf);
            return NULL;
        }
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        buf[i] = (int16_t)v;
    }
    *from_array = true;
    *count = n;
    *bytes = (size_t)n * 2;
    return buf;
}

static esp_err_t write_pcm(const int16_t *mono, size_t frames, bool stereo)
{
    if (!s_ready || !s_tx)
        return ESP_ERR_INVALID_STATE;
    size_t out_frames = 0;
    int16_t *stereo_buf = NULL;
    const int16_t *src = mono;
    if (!stereo) {
        stereo_buf = malloc(frames * 2 * sizeof(int16_t));
        if (!stereo_buf)
            return ESP_ERR_NO_MEM;
        for (size_t i = 0; i < frames; i++) {
            stereo_buf[i * 2] = mono[i];
            stereo_buf[i * 2 + 1] = mono[i];
        }
        src = stereo_buf;
        out_frames = frames * 2;
    } else {
        out_frames = frames;
    }

    size_t total = out_frames * sizeof(int16_t);
    size_t sent = 0;
    esp_err_t err = ESP_OK;
    while (sent < total) {
        size_t chunk = total - sent;
        if (chunk > 4096)
            chunk = 4096;
        size_t written = 0;
        err = i2s_channel_write(s_tx, (const uint8_t *)src + sent, chunk, &written,
                                pdMS_TO_TICKS(2000));
        if (err != ESP_OK || written == 0)
            break;
        sent += written;
    }
    free(stereo_buf);
    return err;
}

/* ---- OGG/Opus playback ------------------------------------------------- */

#define OGG_CHUNK 512
#define OGG_OUT_BUF 8192

static bool s_codec_ready;

static esp_err_t codec_ensure(void)
{
    if (s_codec_ready)
        return ESP_OK;
    esp_audio_err_t ret = esp_opus_dec_register();
    if (ret == ESP_AUDIO_ERR_OK)
        ret = esp_ogg_dec_register();
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "codec register failed: %d", ret);
        return ESP_FAIL;
    }
    s_codec_ready = true;
    return ESP_OK;
}

/* Decode one PCM chunk and write it to the 24 kHz mono I2S output.  Opus
 * normally produces 48 kHz; use linear interpolation for the 2:1 downmix and
 * downmix stereo if the stream is not mono. */
static esp_err_t play_pcm_chunk(const uint8_t *pcm, size_t len, int rate, int channels)
{
    if (!pcm || rate <= 0 || channels <= 0 || len < sizeof(int16_t))
        return ESP_OK;

    size_t frames = (len / sizeof(int16_t)) / (size_t)channels;
    if (frames == 0)
        return ESP_OK;

    const int16_t *src = (const int16_t *)pcm;
    const int16_t *in = src;
    int16_t *mono = NULL;
    if (channels != 1) {
        mono = malloc(frames * sizeof(int16_t));
        if (!mono)
            return ESP_ERR_NO_MEM;
        for (size_t i = 0; i < frames; i++) {
            int sum = 0;
            for (int c = 0; c < channels; c++)
                sum += src[i * (size_t)channels + (size_t)c];
            mono[i] = (int16_t)(sum / channels);
        }
        in = mono;
    }

    size_t out_frames = (size_t)(((uint64_t)frames * SAMPLE_RATE) / (uint32_t)rate);
    if (out_frames == 0)
        out_frames = 1;
    int16_t *out = malloc(out_frames * sizeof(int16_t));
    if (!out) {
        if (channels != 1)
            free(mono);
        return ESP_ERR_NO_MEM;
    }

    if (rate == SAMPLE_RATE) {
        out_frames = frames;
        memcpy(out, in, frames * sizeof(int16_t));
    } else {
        double step = (double)rate / (double)SAMPLE_RATE;
        for (size_t i = 0; i < out_frames; i++) {
            double pos = (double)i * step;
            size_t idx = (size_t)pos;
            if (idx >= frames)
                idx = frames - 1;
            double frac = pos - (double)idx;
            int16_t a = in[idx];
            int16_t b = (idx + 1 < frames) ? in[idx + 1] : a;
            out[i] = (int16_t)((double)a + ((double)b - (double)a) * frac);
        }
    }

    esp_err_t err = write_pcm(out, out_frames, false);
    free(out);
    if (channels != 1)
        free(mono);
    return err;
}

static esp_err_t play_ogg_data(const uint8_t *data, size_t len)
{
    esp_err_t err = codec_ensure();
    if (err != ESP_OK)
        return err;

    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_OGG,
        .dec_cfg = NULL,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t dec = NULL;
    esp_audio_err_t ret = esp_audio_simple_dec_open(&cfg, &dec);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "ogg open failed: %d", ret);
        return ESP_FAIL;
    }

    uint8_t *out = malloc(OGG_OUT_BUF);
    if (!out) {
        esp_audio_simple_dec_close(dec);
        return ESP_ERR_NO_MEM;
    }
    size_t cap = OGG_OUT_BUF;
    int rate = 0;
    int channels = 0;
    bool info_ok = false;
    esp_audio_simple_dec_raw_t raw = {
        .buffer = (uint8_t *)data,
        .len = (uint32_t)len,
        .eos = true,
    };

    while (raw.len && ret == ESP_AUDIO_ERR_OK) {
        esp_audio_simple_dec_out_t frame = {
            .buffer = out,
            .len = cap,
        };
        ret = esp_audio_simple_dec_process(dec, &raw, &frame);
        if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame.needed_size > cap) {
            uint8_t *nb = realloc(out, frame.needed_size);
            if (!nb) {
                ret = ESP_AUDIO_ERR_MEM_LACK;
                break;
            }
            out = nb;
            cap = frame.needed_size;
            continue;
        }
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "ogg decode failed: %d", ret);
            break;
        }
        if (frame.decoded_size) {
            if (!info_ok) {
                esp_audio_simple_dec_info_t info = { 0 };
                if (esp_audio_simple_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK) {
                    rate = (int)info.sample_rate;
                    channels = info.channel;
                    info_ok = true;
                    ESP_LOGI(TAG, "ogg: %d Hz, %d ch, %d bit", rate, channels,
                             info.bits_per_sample);
                }
            }
            if (rate > 0 && channels > 0) {
                esp_err_t werr = play_pcm_chunk(out, frame.decoded_size, rate, channels);
                if (werr != ESP_OK) {
                    ESP_LOGE(TAG, "ogg write failed: %s", esp_err_to_name(werr));
                    err = werr;
                    ret = ESP_AUDIO_ERR_FAIL;
                    break;
                }
            }
        }
        if (raw.consumed == 0) {
            ESP_LOGW(TAG, "ogg stalled: len=%lu decoded=%lu", (unsigned long)raw.len,
                     (unsigned long)frame.decoded_size);
            ret = ESP_AUDIO_ERR_FAIL;
            break;
        }
        raw.len -= raw.consumed;
        raw.buffer += raw.consumed;
    }

    esp_audio_simple_dec_close(dec);
    free(out);
    if (ret != ESP_AUDIO_ERR_OK && err == ESP_OK)
        err = ESP_FAIL;
    return err;
}

static JSValue js_play_ogg(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    char path[128] = { 0 };
    if (argc > 0 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf sbuf;
        size_t plen = 0;
        const char *p = JS_ToCStringLen(ctx, &plen, argv[0], &sbuf);
        if (p && plen < sizeof(path)) {
            memcpy(path, p, plen);
            path[plen] = 0;
        }
    }

    esp_err_t err = audio_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "audio init: %s", esp_err_to_name(err));

    if (!path[0])
        return mjs_throw(ctx, "playOgg: missing path");

    size_t len = 0;
    uint8_t *data = mjs_read_file(path, &len);
    if (!data || len == 0) {
        free(data);
        return mjs_throw(ctx, "%s not found", path);
    }

    int64_t start = esp_timer_get_time();
    err = play_ogg_data(data, len);
    free(data);
    if (err != ESP_OK)
        return mjs_throw(ctx, "ogg playback: %s", esp_err_to_name(err));
    return JS_NewInt32(ctx, (int)((esp_timer_get_time() - start) / 1000));
}

static int arg_int(JSContext *ctx, int argc, JSValue *argv, int idx, int def)
{
    int v = def;
    if (idx < argc)
        JS_ToInt32(ctx, &v, argv[idx]);
    return v;
}

static bool arg_bool(JSContext *ctx, int argc, JSValue *argv, int idx, bool def)
{
    if (idx >= argc)
        return def;
    if (JS_IsBool(argv[idx]))
        return argv[idx] != JS_FALSE;
    int v = 0;
    if (JS_ToInt32(ctx, &v, argv[idx]))
        return def;
    return v != 0;
}

/* ---- JS bindings ------------------------------------------------------- */

static JSValue js_init(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    esp_err_t err = audio_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "audio init: %s", esp_err_to_name(err));
    return JS_TRUE;
}

static JSValue js_ready(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)argc; (void)argv; (void)opaque;
    return JS_NewBool(s_ready);
}

static JSValue js_play(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "audio.play(samples, stereo=false)");
    esp_err_t err = audio_hw_init();
    if (err != ESP_OK)
        return mjs_throw(ctx, "audio init: %s", esp_err_to_name(err));

    int count = 0;
    size_t bytes = 0;
    bool from_array = false;
    int16_t *buf = get_samples(ctx, argv[0], &count, &bytes, &from_array);
    if (!buf)
        return JS_ThrowTypeError(ctx, "samples must be a string or number array");
    bool stereo = arg_bool(ctx, argc, argv, 1, false);
    size_t frames = from_array ? (size_t)count : bytes / 2;
    if (stereo)
        frames /= 2;

    int64_t start = esp_timer_get_time();
    err = write_pcm(buf, frames, stereo);
    free(buf);
    if (err != ESP_OK)
        return mjs_throw(ctx, "audio write: %s", esp_err_to_name(err));
    return JS_NewInt32(ctx, (int)((esp_timer_get_time() - start) / 1000));
}

static JSValue js_tone(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int freq = arg_int(ctx, argc, argv, 0, 440);
    int ms = arg_int(ctx, argc, argv, 1, 500);
    int vol = arg_int(ctx, argc, argv, 2, 30);
    if (freq < 20 || freq > 12000 || ms < 10 || ms > 5000)
        return JS_ThrowRangeError(ctx, "tone(freq, ms, volume)");
    size_t frames = (size_t)SAMPLE_RATE * (size_t)ms / 1000;
    int16_t *buf = malloc(frames * sizeof(int16_t));
    if (!buf)
        return JS_ThrowInternalError(ctx, "out of memory");
    double amp = 32767.0 * (vol > 100 ? 100 : vol) / 100.0;
    for (size_t i = 0; i < frames; i++) {
        double t = (double)i / SAMPLE_RATE;
        /* short fade in/out to avoid clicks */
        double env = 1.0;
        size_t fade = SAMPLE_RATE / 100;
        if (i < fade) env = (double)i / fade;
        if (frames - i < fade) env = (double)(frames - i) / fade;
        buf[i] = (int16_t)(sin(2.0 * M_PI * freq * t) * amp * env);
    }
    esp_err_t err = audio_hw_init();
    if (err == ESP_OK)
        err = write_pcm(buf, frames, false);
    free(buf);
    if (err != ESP_OK)
        return mjs_throw(ctx, "tone: %s", esp_err_to_name(err));
    return JS_NewInt32(ctx, ms);
}

static JSValue js_record(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int ms = arg_int(ctx, argc, argv, 0, 1000);
    if (ms < 10 || ms > 5000)
        return JS_ThrowRangeError(ctx, "record(10..5000)");
    esp_err_t err = audio_hw_init();
    if (err != ESP_OK || !s_rx)
        return mjs_throw(ctx, "audio not ready: %s", esp_err_to_name(err));
    size_t frames = (size_t)SAMPLE_RATE * (size_t)ms / 1000;
    size_t total = frames * 2 * sizeof(int16_t);
    int16_t *stereo = malloc(total);
    if (!stereo)
        return JS_ThrowInternalError(ctx, "out of memory");
    size_t got = 0;
    while (got < total) {
        size_t n = 0;
        err = i2s_channel_read(s_rx, (uint8_t *)stereo + got, total - got, &n,
                               pdMS_TO_TICKS(2000));
        if (err != ESP_OK || n == 0)
            break;
        got += n;
    }
    size_t got_frames = got / (2 * sizeof(int16_t));
    JSGCRef a_ref;
    JSValue *a = JS_PushGCRef(ctx, &a_ref);
    *a = JS_NewArray(ctx, 0);
    for (size_t i = 0; i < got_frames; i++)
        JS_SetPropertyUint32(ctx, *a, (uint32_t)i,
                             JS_NewInt32(ctx, (stereo[i * 2] + stereo[i * 2 + 1]) / 2));
    free(stereo);
    JSValue ret = *a;
    JS_PopGCRef(ctx, &a_ref);
    return ret;
}

static JSValue js_set_volume(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    int vol = arg_int(ctx, argc, argv, 0, 70);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_ready)
        codec_write(0x32, (uint8_t)(vol * 255 / 100));
    return JS_NewInt32(ctx, vol);
}

static JSValue js_mute(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, void *opaque)
{
    (void)this_val; (void)opaque;
    bool mute = arg_bool(ctx, argc, argv, 0, false);
    if (s_ready)
        codec_update(0x31, 0x60, mute ? 0x60 : 0x00);
    return JS_UNDEFINED;
}

void mjs_module_audio_init(JSContext *ctx, const mjs_audio_pins_t *pins)
{
    if (pins)
        s_pins = *pins;
    mjs_module_begin(ctx, "audio");
    mjs_module_add_fn(ctx, "audio", "init", js_init, NULL);
    mjs_module_add_fn(ctx, "audio", "ready", js_ready, NULL);
    mjs_module_add_fn(ctx, "audio", "play", js_play, NULL);
    mjs_module_add_fn(ctx, "audio", "playOgg", js_play_ogg, NULL);
    mjs_module_add_fn(ctx, "audio", "tone", js_tone, NULL);
    mjs_module_add_fn(ctx, "audio", "record", js_record, NULL);
    mjs_module_add_fn(ctx, "audio", "setVolume", js_set_volume, NULL);
    mjs_module_add_fn(ctx, "audio", "mute", js_mute, NULL);
}
