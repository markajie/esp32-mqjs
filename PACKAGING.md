# 打包说明

## 建议打包的内容

```text
CMakeLists.txt
README.md
PACKAGING.md
sdkconfig.defaults
dependencies.lock
main/
components/
tools/
docs/
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

# 选板子并编译/烧录；每块板子用独立 build 目录和独立 sdkconfig
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 build
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 -p COM3 flash
```

芯片目标、flash 大小、分区表和 PSRAM 都由板子自己的
`main/boards/<vendor>/<board>/sdkconfig.defaults.board` 提供（分区表文件也
放在该板目录里），不需要 `set-target`，也不需要手动 menuconfig。sdkconfig
按板子存成 `sdkconfig.<vendor>_<board>`，切换板子不会串配置。

直接烧录 `firmware/` 里的已验证固件：

```powershell
cd firmware
python -m esptool --chip esp32c5 -p COM3 -b 460800 write_flash '@flash_args'
```

> PowerShell 里 `@flash_args` 要加引号，否则会被当成 splatting。

## IDF 6.1 兼容说明

`components/gen_bmgr_codes/` 由 esp_board_manager 根据所选板子的 YAML 生成，
被 git 忽略。CMake 会在板子或 YAML 变化时自动重新生成；全新克隆后第一次
`idf.py ... build` 需要先下载托管组件，因此会停下并提示再执行一次。

IDF 6.1 删除了 RMT 配置里的 `io_loop_back` 和 `io_od_mode` 字段。当前
esp_board_manager 0.6.2 的 idf6 模板已经不再生成它们；如果重新生成后仍报
这两个字段，说明用了旧的模板，删掉这两行或升级 esp_board_manager。

本版本删除了内嵌测试音 `welcome.ogg`。`audio.playOgg(path)` 现在必须传
文件路径，文件不存在会抛错；需要开机音时自己把 OGG 上传到 `/spiffs/`。

## 对外发布前

- `main/js/main.js` 含恢复热点的 SSID/密码（默认 `Moji2-C5` / `12345678`），
  对外发布前请改成自己的值。
- 当前板子的 `/spiffs/user.js` 是时钟脚本，不在本工程目录内；
  备份 zip 里的 `current_spiffs_user.js` 也含 WiFi 密码，不要一起外发。
- `firmware/my_esp32_mqjs.bin` 是去掉 LVGL/display_service 后的版本；
  旧版回滚包在 `D:\esp\backups\my-esp32-mqjs\mqjs-good-2026-11-01_2033.zip`。
