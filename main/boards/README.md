# Boards

Every board is one self-contained directory.  Adding or changing a board never
edits a shared source file; everything the board needs lives here.

```
main/boards/<vendor>/<board>/
├── board_info.yaml           # identity: board id, chip, manufacturer (required)
├── board_peripherals.yaml    # buses and pins owned by esp_board_manager
├── board_devices.yaml        # devices owned by esp_board_manager (LCD, backlight)
├── sdkconfig.defaults.board  # target, flash size, partition table, PSRAM
├── partitions.csv            # optional: board-specific layout
├── config.h                  # board-private pin macros
├── board.c                   # implements main/boards/board.h
├── setup_device.c            # board-private driver hook (e.g. LCD panel factory)
└── README.md
```

`main/boards/board.h` is the shared contract: `board_get()` returns the
identity and the pins the shared modules need, `board_init()` performs early
bring-up.  A board reports hardware it does not have as `GPIO_NUM_NC` (or
`-1`); shared code skips those pins.

## Adding a board

1. Create `main/boards/<vendor>/<board>/` with `board_info.yaml`, the two
   hardware YAML files and `sdkconfig.defaults.board`.
2. Add `config.h` and `board.c`; include `"../../board.h"` in `board.c` and
   fill the struct from `config.h`.  Put chip-specific driver code (panel
   factory, vendor init tables, a codec's register sequence) in the board
   directory too.  A board with an audio codec implements `board_codec()`;
   the shared audio module supplies the I2C adapter and calls it.
3. Build with `idf.py -DBOARD=<vendor>/<board> -B build/<board> build`.

Nothing else needs editing.  The build discovers boards by `board_info.yaml`,
compiles only the selected board directory, and derives its sdkconfig from the
board file.

## How a board gets compiled

| Source | Compiled by | Use for |
| --- | --- | --- |
| top-level `*.c` in the board directory | generated `components/gen_bmgr_codes` component | `board.c`, `setup_device.c`, board-private devices |
| `components/**` | ESP-IDF components | code that must behave the same on every board |

`SRC_DIRS` is not recursive, so a board's `.c` files are picked up while
sibling boards stay out of the build.  This project is C only; `.cc`/`.cpp`
files in a board directory are not compiled.

## Selecting a board

```powershell
idf.py -DBOARD=movecall/moji2-esp32c5 -B build/c5 build
idf.py "-DBOARD=zhengchen/1.54tft-wifi" -B build/s3 build
idf.py -DBOARD=list build        # list the available boards
```

Quote the value in PowerShell when it contains a dot: an unquoted
`-DBOARD=zhengchen/1.54tft-wifi` is split after `1`.  `$env:BOARD` works too:

```powershell
$env:BOARD = 'zhengchen/1.54tft-wifi'
idf.py -B build/s3 build
```

The selection is cached in the build directory.  With exactly one board in the
tree it is selected implicitly.  Because the chip target changes between
boards, give each board its own build directory.

## Troubleshooting

A boot loop whose ROM log says `invalid header` means the bootloader is not
the flashed image (an interrupted or app-only flash).  Run a full
`idf.py ... flash`; it writes the bootloader, partition table and app together.

If the panel stays dark for several minutes after a full flash, the first boot
is formatting the SPIFFS `storage` partition; wait it out instead of resetting.

If the board has no PSRAM and the log says `cannot allocate ... byte JS heap`,
the board's `sdkconfig.defaults.board` does not describe its RAM.  Enable
`CONFIG_SPIRAM` with the right mode, or size the heap to the internal RAM that
is actually available.

## sdkconfig

Each board keeps `sdkconfig.<vendor>_<board>` at the project root, so switching
boards never mixes target, flash or PSRAM settings.  The chip target is read
from the board's `sdkconfig.defaults.board`, so no explicit `idf.py set-target`
is needed.  Delete that file to regenerate it from the defaults.

## Generated board component

`components/gen_bmgr_codes/` is produced by esp_board_manager from the selected
board's YAML files and is ignored by git.  CMake regenerates it automatically
when the board or one of its YAML/default files changes.  Because that component
is shared, build one board at a time (switching boards regenerates it).  On a
fresh checkout the first configure has to download `esp_board_manager` first,
so the build stops once with an explanation; run the same command again.
