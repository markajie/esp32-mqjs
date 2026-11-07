# ESP32-C5 MQuickJS firmware

Moji 2 (Movecall) ESP32-C5 board firmware with [MQuickJS](https://github.com/bellard/mquickjs) as the on-device JavaScript engine.

- `components/mquickjs/mquickjs/` is the upstream checkout and is **not modified**.
- `components/mquickjs/mqjs_engine/` binds MQuickJS to ESP-IDF (heap, timers, event queue, SPIFFS, native dispatch).
- `components/mqjs_modules/` contains the JavaScript-facing hardware modules.
- `main/boards/<vendor>/<board>/` contains each board; `main/boards/board.h` is the shared board contract, and `main/main.c` boots the engine and runs `main/js/main.js`.
- `main/js/main.js` is embedded into the firmware and written to `/spiffs/main.js` when the file is missing or differs from the embedded copy.
- Practical JS examples (WiFi, MQTT, WebSocket, audio recording, HTTP, BLE) are in
  [`docs/JS_EXAMPLES.md`](docs/JS_EXAMPLES.md).

## Build

Use the existing ESP-IDF installation (no toolchain installation needed) and
select the board once:

```powershell
$env:IDF_TOOLS_PATH = 'D:\Espressif'
. 'D:\esp\v6.1\esp-idf\export.ps1'

idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 build
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 -p COMx flash monitor
```

Every board keeps its own build directory and sdkconfig, and the board's
`sdkconfig.defaults.board` selects the chip target, flash size, PSRAM and
partition table, so no `set-target` or menuconfig step is needed.  See
[`main/boards/README.md`](main/boards/README.md) for the full board workflow,
including the one extra build run on a fresh checkout while
`esp_board_manager` is downloaded.

The Moji 2 16 MB layout (`partitions_16MB.csv`) gives the app 5 MB and SPIFFS
about 10.9 MB as the `storage` partition. Flash runs DIO at 80 MHz and PSRAM
quad at 80 MHz. QIO corrupts PSRAM reads on this board revision (green-tinted
framebuffer), so the board defaults pin DIO.

## Dependencies

The MQTT, WebSocket and audio-codec libraries are managed by the IDF Component
Manager instead of being copied into `components/`:

| Component | Version |
| --- | --- |
| `espressif/esp_audio_codec` | 2.6.2 |
| `espressif/esp_board_manager` | 0.6.x |
| `espressif/mqtt` | 1.1.0 |
| `espressif/esp_websocket_client` | 1.8.0 |

`main/idf_component.yml` declares the direct dependencies; `dependencies.lock`
records the exact resolved versions. `idf.py build` downloads them into
`managed_components/`, which is generated and should not be edited. To refresh or
upgrade, edit the manifest and run `idf.py reconfigure` (or `idf.py update-dependencies`).

The embedded `main.js` is the factory recovery script. At boot the engine checks
`/spiffs/user.js` first; if it exists, it runs that script. If the user script throws,
the engine logs the exception and falls back to the embedded `main.js`, which clears
any timers the failed script left behind, so the repair page remains reachable. `main.js` needs no network
and no credentials: it starts a WPA2 access point and the uploader immediately.

### Changing JS at runtime

In recovery mode the panel shows the access point and the page address:

```text
SSID:     Moji2-C5
Password: 12345678
Page:     http://192.168.4.1/
```

Join that network from a phone or laptop and open the page. Recovery mode is
AP-only, so it works with no router; a `user.js` that needs an existing network
calls `wifi.connect()` itself. The page contains the current script. Edit it,
click **Save & reboot**, and the board writes `/spiffs/user.js` and reboots
into it. **No firmware rebuild or flash is needed.** **Delete user.js &
reboot** restores the embedded recovery script.

The page has no authentication and is intended for a trusted local network. Keep
`web.start(80)` in a user script if you want to keep editing it over the air.

If a bad `user.js` hangs the board, hold **BOOT** while resetting. The firmware
skips `/spiffs/user.js` for that boot and runs the embedded `main.js`, so the
recovery page comes back and the bad script can be replaced or deleted.

### First build / clean build

A fresh clone has no `managed_components/` and no
`components/gen_bmgr_codes/`.  The first `idf.py ... build` downloads the
managed components and then stops so the board component can be generated; run
the same command again.  After that, CMake regenerates the board component
whenever the selected board or one of its YAML/default files changes.

The first boot after a full flash, or after changing the partition table,
reformats the SPIFFS `storage` partition.  Formatting is slow (roughly
30 KB/s): an 11 MB partition takes a few minutes and a 13 MB one around seven,
and the display stays dark until it finishes.  Do not power-cycle during that
window; later boots reach the recovery page in about 3.5 seconds.

### Changing main.js offline

If the web page is not running (first boot or recovery), edit `main/js/main.js` and
use an incremental build/flash:

```powershell
# edit main/js/main.js first
idf.py build
idf.py -p COMx flash monitor
```

No C file has to be edited for normal application changes. `main/js/main.js` is
embedded with `target_add_binary_data`, so `idf.py build flash` is enough. A full
clean is only needed after changing `sdkconfig` or the partition table. If a change
adds a new peripheral, protocol, decoder or other native capability, a C module
must be added first; JS can then use it.

## JavaScript API

All modules are global objects. Errors are thrown as JS exceptions.

### system

`chip()`, `mac()`, `freeHeap()`, `minFreeHeap()`, `freePsram()`,
`uptime()`, `reboot()`, `delay(ms)`, `time()`, `setTime(ms)`,
`log(text)`, `resetReason()`

### fs (SPIFFS, paths include `/spiffs/`)

`exists(path)`, `read(path)`, `readBytes(path)`, `write(path, data)`,
`remove(path)`, `rename(old, new)`, `size(path)`, `list(path)`, `info()`

### gpio

`mode(pin, mode)` where mode is 0 input, 1 output, 2 input-pullup, 3 input-pulldown, 4 open-drain output;
`write(pin, value)`, `read(pin)`, `toggle(pin)`

### i2c

`init(sda, scl, hz?)`, `ready()`, `write(addr, data)`, `read(addr, len)`,
`writeReg(addr, reg, data)`, `readReg(addr, reg, len?)`, `scan()`

### display (panel described by the board)

`init()`, `ready()`, `width()`, `height()`, `color(r,g,b)`,
`fill(color)`, `clear(color)`, `fillRect(x,y,w,h,color)`, `pixel(x,y,color)`,
`line(x0,y0,x1,y1,color)`, `rect(x,y,w,h,color)`, `circle(cx,cy,r,color)`,
`text(x,y,text,color,bg?,scale?)`, `textWidth(text,scale?)`,
`bitmap(x,y,w,h,data)`,
`flush()` / `present()`, `backlight(on)`

Drawing goes to a PSRAM RGB565 framebuffer; call `flush()` (or `fill`/`clear`) to
send it to the panel. `text` returns the widest drawn line; `\n` starts a new line
and `bg = -1` keeps the background transparent. `scale` is clamped to 1..4.
`textWidth` measures without drawing, which is what centered layouts should use.

The panel init table stays in the board manager: it reads the selected board's
`board_devices.yaml` + `board_peripherals.yaml`, and the board's
`setup_device.c` supplies the chip-specific panel factory (the Moji 2 one also
carries the ST77916 vendor init sequence). `display_panel` takes the board's
`esp_lcd` panel/IO handles,
draws into the PSRAM framebuffer, and stages each strip through a 4 KB internal
DMA buffer; it swaps RGB565 bytes when the board's `data_endian` is big and
waits for the panel IO completion callback. No LVGL or `display_service` layer
is linked. A wrong colour is a board-YAML / `data_endian` issue, not something
scripts should compensate for. The backlight is the board manager's
`lcd_brightness` LEDC device.

Text uses a 24 px Noto Sans SC bitmap atlas (3,682 glyphs: printable ASCII plus
3,587 CJK, one face for Latin and CJK so weight and baseline match). Digits and
`: . , - +` also exist in a 96 px anti-aliased companion set drawn 1:1 at scale 4
and downsampled 2x2 at scale 2, so large clock digits stay smooth; other
characters fall back to plain magnification at those scales. Regenerate
`components/mqjs_modules/mjs_driver_display/font_atlas.h` with
`components/mqjs_modules/mjs_driver_display/tools/gen_font.ps1` after changing
`components/mqjs_modules/mjs_driver_display/tools/fonts/NotoSansSC-Regular.ttf`
or `tools/cjk_codepoints.txt`.

### audio (I2S + optional board codec, 24 kHz stereo)

`init()`, `ready()`, `play(samples, stereo?)`, `playOgg(path)`,
`tone(freq, ms, volume?)`, `record(ms)`, `setVolume(0..100)`, `mute(on)`

The codec is board-owned: the Moji 2's ES8311 register sequence, volume and mute
live in `main/boards/movecall/moji2-esp32c5/es8311_codec.c`.  The shared module
drives I2S, calls `board_codec()` over a small I2C adapter, and falls back to
I2S-only when the board returns `NULL`.

`samples` is an array of signed 16-bit values (mono by default) or a raw PCM string.
`playOgg` decodes an OGG/Opus file from SPIFFS and plays it at 24 kHz. The path is
required and a missing file raises an error; the firmware does not embed an audio
asset, so upload `/spiffs/welcome.ogg` yourself if you want a startup sound. It
returns the elapsed milliseconds.

`record(ms)` is blocking, accepts 10..5000 ms, and returns a JS array of averaged
mono signed 16-bit samples at 24 kHz. For 500 ms that is 12,000 values. The JS heap
is 512 KB, so prefer short chunks (250..1000 ms); a full 5-second take is 120,000
values and can exhaust the heap. See
[`docs/JS_EXAMPLES.md`](docs/JS_EXAMPLES.md#7-音频播放与录音) for a peak meter and
raw-PCM save example.

### wifi

`init()`, `connect(ssid, password?, timeoutMs?)`, `disconnect()`, `status()`,
`ip()`, `mac()`, `scan()`, `hostname(name?)`, `startAP(ssid, password?)`

### net

`request({url, method, body, contentType, headers, timeout})`,
`get(url, headers?, timeout?)`, `post(url, body, contentType?, headers?)`,
`resolve(host)`, `sntp(server?)`

`headers` is a newline-separated string such as `"Authorization: Bearer x\nAccept: */*"`.
HTTPS uses the ESP-IDF certificate bundle.

### mqtt (MQTT 3.1.1 over TCP or TLS)

`connect(url, clientId?, username?, password?)`, `publish(topic, payload, qos?, retain?)`,
`subscribe(topic, qos?)`, `onMessage(fn)`, `onConnect(fn)`, `onDisconnect(fn)`,
`connected()`, `disconnect()`

`url` uses `mqtt://host:1883` or `mqtts://host:8883`. The client must be started after
WiFi has an IP; otherwise `connect` throws. `onMessage` receives
`(topic, payload)`. TLS uses the ESP certificate bundle. Payloads are delivered in
one callback up to 8 KB; larger messages are truncated.

```js
mqtt.onMessage(function (topic, payload) {
  print("mqtt:", topic, payload);
});
mqtt.onConnect(function () {
  mqtt.subscribe("c5/demo", 0);
  mqtt.publish("c5/demo", "hello", 0);
});
mqtt.connect("mqtt://broker.example.com:1883", "c5-demo");
```

### ws (WebSocket client)

`connect(url)`, `send(text)`, `sendBinary(data)`, `onMessage(fn)`, `onConnect(fn)`,
`onDisconnect(fn)`, `connected()`, `close()`

`ws://` and `wss://` are supported; `wss` uses the certificate bundle. `onMessage`
receives the text/binary frame as a string. Start it after WiFi is connected.
Each callback carries at most 8 KB; fragmented frames are not reassembled.

### ble (NimBLE UART-style GATT service)

`init(name?)`, `name(name?)`, `address()`, `connected()`, `send(text)`,
`onReceive(fn)`, `stop()`. The receive callback is called as `fn(data)` with a
binary string (up to 512 bytes).

Service UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, RX `...0002...`,
TX (notify) `...0003...`.

### web (JS uploader / editor)

`start(port?)` (default 80), `stop()`, `ready()`

- `GET /` serves an editor containing `/spiffs/user.js` (or `/spiffs/main.js`).
- `POST /save` writes the request body to `/spiffs/user.js` and reboots.
- `POST /reset` deletes `/spiffs/user.js` and reboots.

The uploader runs in the ESP HTTP server task. The recovery `main.js` starts it
at boot; a user script must call `web.start(80)` itself.

### board

`name`, `manufacturer`, `target`, `width`, `height`, `ledPin`, `bootPin`

### timers

`setTimeout(fn, ms)`, `clearTimeout(id)`, plus a JS polyfill for
`setInterval` / `clearInterval`.

## Board configuration

Everything board-specific lives under `main/boards/<vendor>/<board>/`:
`config.h` holds the pin map, `board.c` implements the shared contract in
`main/boards/board.h`, `setup_device.c` holds private driver code, the YAML
files describe what esp_board_manager brings up, and `sdkconfig.defaults.board`
selects the chip and flash layout.  For the Moji 2, `config.h` contains QSPI
display (SCLK 0, CS 3, RST 1, D0..D3 9/8/7/6), I2C 26/27, I2S MCLK 25 / WS 24 /
BCLK 11 / DIN 12 / DOUT 23, PA 5, LED 10, BOOT 28 and backlight 2.

To support another board, drop in a new `main/boards/<vendor>/<board>/`
directory and select it with `-DBOARD`; no shared file changes. See
[`main/boards/README.md`](main/boards/README.md).

## Third-party code

- `components/mquickjs/mquickjs/`: Fabrice Bellard's MQuickJS (upstream checkout, unmodified).
- `espressif/esp_lcd_st77916` (managed): ST77916 panel driver from Espressif (Apache-2.0), used by the Moji 2 board.
- `espressif/esp_audio_codec` (managed): Espressif audio codec (OGG/Opus decoder), Espressif Modified MIT license; see `managed_components/espressif__esp_audio_codec/LICENSE`.
- `components/mqjs_modules/mjs_driver_display/tools/fonts/NotoSansSC-Regular.ttf`: Noto Sans SC, SIL Open Font License 1.1; see https://fonts.google.com/noto/specimen/Noto+Sans+SC. The board's display tool `gen_font.ps1` bakes it into a 24 px monochrome atlas.
- `managed_components/`: downloaded by the IDF Component Manager; each component
  keeps its own license and source. Do not edit these generated copies.
  `espressif/mqtt` and `espressif/esp_websocket_client` are Apache-2.0.

## Generated headers

`components/mquickjs/mqjs_headers/` contains `mqjs_stdlib.h` and
`mquickjs_atom.h` generated by `components/mquickjs/tools/gen_mquickjs_headers.py`
(a Python port of `mquickjs_build.c`). Regenerate after changing the custom
stdlib:

```powershell
riscv32-esp-elf-gcc -E -P -I components/mquickjs/mquickjs \
  components/mquickjs/mqjs_engine/mjs_stdlib_build.c -o $env:TEMP/mjs_stdlib.i
python components/mquickjs/tools/gen_mquickjs_headers.py --preprocessed $env:TEMP/mjs_stdlib.i
```
