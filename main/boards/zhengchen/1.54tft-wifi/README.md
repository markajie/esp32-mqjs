# ZhengChen 1.54" TFT WiFi (ESP32-S3)

240x240 ST7789 over SPI3, LEDC backlight, no codec chip (bare I2S).

## Hardware

| Function | GPIO |
| --- | --- |
| LCD MOSI (SDA) | 41 |
| LCD SCLK | 42 |
| LCD CS | 21 |
| LCD DC | 40 |
| LCD RST | 45 |
| Backlight (PWM) | 20 |
| Boot button | 0 |
| Volume up | 10 |
| Volume down | 39 |
| Speaker BCLK | 15 |
| Speaker WS / LRCK | 16 |
| Speaker DOUT | 7 |
| Mic SCK | 5 |
| Mic WS | 4 |
| Mic DIN | 6 |
| Battery ADC | 9 |

| Feature | Specification |
| --- | --- |
| Chip | ESP32-S3 (QFN56, v0.2) |
| Flash | 16 MB |
| PSRAM | 8 MB octal, 80 MHz (embedded) |

The panel is driven by `esp_lcd_st7789` through `setup_device.c`; resolution,
SPI bus and backlight PWM are declared in `board_peripherals.yaml` and
`board_devices.yaml`, so the shared display module needs no board-specific code.

## Build

```powershell
# PowerShell splits an unquoted 1.54... argument, so quote the whole -DBOARD
idf.py "-DBOARD=zhengchen/1.54tft-wifi" -B build/s3 build
idf.py "-DBOARD=zhengchen/1.54tft-wifi" -B build/s3 -p COMx flash monitor
```

This board's auto-reset does not always enter download mode; hold **BOOT**,
tap **RST**, then release **BOOT** before flashing if `idf.py flash` cannot
connect.  The board directory is discovered from `board_info.yaml`; see
[`../../README.md`](../../README.md) for the shared board workflow.

## Known limits

- **Audio is not wired up yet.** This board has no codec chip: the speaker and
  the microphone use two independent I2S clocks, while the shared audio module
  drives a single full-duplex I2S link. `board_config_t` reports
  `codec_addr = -1`, so the audio module logs "i2s only" and skips the codec.
  Playback/recording needs a dedicated board path before it will work.
- Buttons are only configured as inputs; the JS layer owns their behaviour.
- The recovery UI is laid out for 360x360; the bottom rows are clipped on this
  240x240 panel.
