# Board initialization

This minimal ESP-IDF 6.1 consumer initializes LCD, touch and backlight through
Board Manager. It does not draw a GUI or initialize audio/SD. Shared SPI
boards acquire resources per device; do not replace this sequence with a
bulk `esp_board_manager_init()` call.

The manifest uses `guition-jc/guition_boards` 0.1.2 through
`override_path: "../../.."`. Replace `/path/to/esp-idf` with your ESP-IDF 6.1
installation directory and run from this example directory:

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32p4
idf.py bmgr -b jc8012p4a1
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash monitor
```

JC8012P4A1C also needs the published `guition-jc/esp_lcd_touch_gsl3680` 0.1.0.
The touch driver is downloaded from the registry without a local override.
When copying this example elsewhere, update the board component override to
its local path, or remove it after `guition-jc/guition_boards` is published.

For another board choose its matching target and BMGR identifier from
`board_catalog.json` (use `bmgr_board` when supplied). For example,
`esp32_2432s024` uses target `esp32`. ESP32 and ESP32-C3 have no PSRAM.
Run menuconfig to choose the available touch variant; currently the existing
P4 boards retain their native touch configuration and have no C/R/N menu.

When changing IDF versions or chips, clear this example's previous build
and `components/gen_bmgr_codes` before `set-target`, then regenerate with
`bmgr`. Never reuse a board's generated configuration for a different chip.
The bundled partition table fits the supported boards' minimum 4 MB flash;
adjust it for your application if necessary. Older ESP32-P4 silicon requires
the matching revision-family settings in menuconfig. Compilation is not
a hardware test.
