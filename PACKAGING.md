# 打包说明

## 建议打包的内容

```text
CMakeLists.txt
README.md
PACKAGING.md
sdkconfig
sdkconfig.defaults
partitions_16MB.csv
dependencies.lock
main/
components/
mquickjs/
tools/
docs/
firmware/
```

## 不要打包的内容

| 路径 | 原因 |
|---|---|
| `build/` | 编译中间产物，已删除；`idf.py build` 可重建 |
| `managed_components/` | 组件管理器缓存（约 57MB），按 `dependencies.lock` 可恢复 |
| `.dsh/` | DSH 工具本地 skill 状态，与固件无关 |
| `backups/` | 已移到 `D:\esp\backups\my-esp32-mqjs\`，含旧版固件和 WiFi 配置 |
| `*.log` | 调试串口/编译日志 |

如果需要在**离线环境**编译，把本机的 `managed_components/` 一并带走即可；
否则保留 `dependencies.lock`，首次构建时组件管理器会重新拉取。

## 编译和烧录

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'
. 'D:\esp\v6.1\esp-idf\export.ps1'
idf.py build
idf.py -p COM3 flash
```

直接烧录 `firmware/` 里的已验证固件：

```powershell
cd firmware
python -m esptool --chip esp32c5 -p COM3 -b 460800 write_flash '@flash_args'
```

> PowerShell 里 `@flash_args` 要加引号，否则会被当成 splatting。

## IDF 6.1 兼容说明

`components/gen_bmgr_codes/` 由 esp_board_manager 生成。IDF 6.1 删除了 RMT
配置里的 `io_loop_back` 和 `io_od_mode` 字段，本仓库已从
`gen_board_periph_config.c` 移除这两行。若重新生成后编译报这两个字段，
删掉它们，或升级 esp_board_manager。

## 对外发布前

- `main/js/main.js` 含恢复热点的 SSID/密码（默认 `Moji2-C5` / `12345678`），
  对外发布前请改成自己的值。
- 当前板子的 `/spiffs/user.js` 是时钟脚本，不在本工程目录内；
  备份 zip 里的 `current_spiffs_user.js` 也含 WiFi 密码，不要一起外发。
- `firmware/my_esp32_mqjs.bin` 是去掉 LVGL/display_service 后的版本；
  旧版回滚包在 `D:\esp\backups\my-esp32-mqjs\mqjs-good-2026-11-01_2033.zip`。
