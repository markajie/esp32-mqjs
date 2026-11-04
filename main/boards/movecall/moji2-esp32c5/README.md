# Movecall Moji2 ESP32-C5

## Hardware Overview

| Feature | Specification |
|---------|---------------|
| Chip | ESP32-C5 |
| Flash | 16MB DIO 80MHz |
| PSRAM | Quad 80MHz |
| Display | ST77916 360x360 QSPI round |
| Audio Codec | ES8311 (I2S + I2C) |
| LED | WS2812 x1 (RMT, no DMA) |
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
cd D:\esp\projects\my-esp32-mqjs

# Generate components/gen_bmgr_codes from these YAML files
python managed_components/espressif__esp_board_manager/gen_bmgr_config_codes.py `
  -b main/boards/movecall/moji2-esp32c5 --project-dir .

# Fresh checkout / no sdkconfig yet: select the C5 target once
idf.py set-target esp32c5

# Build and flash
idf.py build
idf.py -p COMx flash monitor
```

The generated component is ignored by git, so run the generator after a fresh
clone. A normal incremental `idf.py build` reuses it; re-run the generator after
editing `board_devices.yaml`, `board_peripherals.yaml`,
`sdkconfig.defaults.board` or `setup_device.c`. If `managed_components/` is
also missing, run `idf.py reconfigure` once so the component manager downloads
`esp_board_manager`, then run the generator.

## Notes

- Display uses QSPI interface (4 data lines) with 32-bit `lcd_cmd_bits`
- `setup_device.c` contains the full ST77916 vendor initialization sequence (~160 register commands)
- RMT does not support DMA on C5 (`with_dma: false`)
- Flash size and the partition table are applied automatically from `sdkconfig.defaults.board` through CMake. If a build still shows 2MB, delete `sdkconfig` and rebuild; no manual menuconfig is needed.

## Partition Table

Uses `partitions_16MB.csv` for the 16MB flash layout.

## Files

| File | Description |
|------|-------------|
| `board_info.yaml` | Board identity (chip, manufacturer) |
| `board_peripherals.yaml` | Peripheral pin and bus configuration (QSPI) |
| `board_devices.yaml` | Device driver configuration (audio, display, LED) |
| `sdkconfig.defaults.board` | Board-level sdkconfig defaults |
| `setup_device.c` | ST77916 display driver initialization with vendor command sequence |
