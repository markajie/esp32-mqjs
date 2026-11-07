# Movecall Moji2 ESP32-C5

## Hardware Overview

| Feature | Specification |
|---------|---------------|
| Chip | ESP32-C5 |
| Flash | 16MB DIO 80MHz |
| PSRAM | Quad 80MHz |
| Display | ST77916 360x360 QSPI round |
| Audio Codec | ES8311 (I2S + I2C) |
| LED | WS2812 x1 |
| Backlight | LEDC PWM control |

## GPIO Mapping

| Function | GPIO |
|----------|------|
| I2C SDA | 26 |
| I2C SCL | 27 |
| I2S MCLK | 25 |
| I2S BCLK | 11 |
| I2S WS | 24 |
| I2S DOUT | 23 |
| I2S DIN | 12 |
| QSPI D0 | 9 |
| QSPI D1 | 8 |
| QSPI D2 | 7 |
| QSPI D3 | 6 |
| QSPI SCLK | 0 |
| LCD CS | 3 |
| LCD RST | 1 |
| PA Enable | 5 |
| Backlight | 2 |
| WS2812 | 10 |

## Build & Flash

```powershell
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 build
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 -p COMx flash monitor
```

The board directory is discovered from its `board_info.yaml`; see
[`../../README.md`](../../README.md) for the shared board workflow.

## Notes

- Display uses QSPI (4 data lines) with 32-bit `lcd_cmd_bits`; `setup_device.c`
  carries the full ST77916 vendor init sequence (~160 register commands).
- esp_board_manager owns only the panel and its backlight. I2C, I2S and the
  ES8311 codec are driven directly by the shared i2c/audio modules from the
  pins in `config.h`; repeating them in the YAML would duplicate the pin map.
- Flash size, PSRAM and the partition table come from
  `sdkconfig.defaults.board`; no menuconfig or `set-target` step is needed.

## Partition Table

Uses the `partitions_16MB.csv` in this directory (16MB flash).

## Files

| File | Description |
| --- | --- |
| `board_info.yaml` | Board identity (chip, manufacturer) |
| `board_peripherals.yaml` | QSPI bus and backlight PWM |
| `board_devices.yaml` | LCD panel and backlight device |
| `sdkconfig.defaults.board` | Target, flash, PSRAM, partition table |
| `config.h` | Pin map |
| `board.c` | Shared board contract |
| `setup_device.c` | ST77916 panel factory with vendor init sequence |
| `es8311_codec.c` | ES8311 register sequence, volume and mute (`board_codec()`) |
