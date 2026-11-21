# Guition Boards

部分开发板配置可能不完善，会在之后逐渐完善。

如果发现某个开发板配置存在问题，欢迎在 [Guition 仓库](https://github.com/Guition-JC/Guition)中[提交 Issue](https://github.com/Guition-JC/Guition/issues)。

Guition 开发板的 ESP Board Manager 板级支持组件，组件名为 `guition-jc/guition_boards`。

本组件提供开发板信息、外设与设备配置、板级默认 sdkconfig，以及显示、触摸、背光和 SD 卡适配。添加组件后，通过 ESP Board Manager 选择板型并生成配置代码，即可在 ESP-IDF 应用中使用对应设备。

每个板型目录包含以下配置，部分板型还提供设备初始化源码和 Kconfig 选项：

- `board_info.yaml`：板型标识、目标芯片和厂商信息。
- `board_peripherals.yaml`：总线、GPIO 等外设配置。
- `board_devices.yaml`：设备、驱动依赖及初始化参数。
- `sdkconfig.defaults.board`：板级默认配置。

板级 YAML 使用两空格缩进，列表项相对所属字段缩进一级，设备和外设条目之间保留空行。标量数组采用行内形式（如 `[FL, FR]`），I2C 地址采用十六进制（如 `0x30`、`[0xBA, 0x28]`）；描述、厂商、挂载路径和需要引号的字符串使用双引号。

ESP Board Manager 的完整用法见[组件文档](https://github.com/espressif/esp-board-manager/blob/main/README.md)。本文的板型列表组织方式参考 [ESP Boards](https://github.com/espressif/esp-board-manager/blob/main/esp_boards/README.md)。

## 兼容性

当前版本为 `0.1.4`，依赖要求以 [idf_component.yml](idf_component.yml) 为准：

| 依赖 | 版本 |
| --- | --- |
| ESP-IDF | `>=6.1.0,<6.2.0` |
| `espressif/esp_board_manager` | `0.7.3~1` |
| `espressif/esp_lcd_touch` | `1.2.1` |

组件清单声明的目标为 ESP32、ESP32-C3、ESP32-S3、ESP32-P4 和 ESP32-S31，共 24 个板型配置。ESP32-S31 板型需要所用 ESP-IDF 环境提供 `esp32s31` 目标支持。

## 支持的开发板

下表描述本组件提供的配置能力，板型标识可直接用于 `idf.py bmgr -b <board>`。触摸列列出可选硬件变体，应按实际硬件选择；`-` 表示未提供对应设备配置。板型配置不代表已完成逐板实机验证。

### ESP32-P4

| 板型标识 | LCD（接口） | 分辨率 | 触摸 | SD 卡 | 音频 |
| --- | --- | --- | --- | --- | --- |
| [jc1060p470](jc1060p470/) | JD9165（MIPI DSI） | 1024×600 | GT911 | SDMMC | ES8311（DAC + ADC） |
| [jc1060p470_i_w_y](jc1060p470_i_w_y/) | JD9165（MIPI DSI） | 1024×600 | GT911 | SDMMC | ES8311（DAC + ADC） |
| [jc4880p443](jc4880p443/) | ST7701（MIPI DSI） | 480×800 | GT911 | SDMMC | ES8311（DAC + ADC） |
| [jc8012p4a1](jc8012p4a1/) | JD9365（MIPI DSI） | 800×1280 | GSL3680 | SDMMC | ES8311（DAC + ADC） |

`jc1060p470` 和 `jc1060p470_i_w_y` 分别对应无外壳与带外壳版本。JC8012P4A1 可在 menuconfig 的 `Guition JC8012P4A1 display` 中选择 LCD V1 / V2，默认 V2。JC4880P443 的触摸列以实际 GT911 工厂函数为准；其 YAML 芯片名与坐标范围仍需结合硬件核对，详见[板级配置](jc4880p443/board_devices.yaml)。

### ESP32-S31

| 板型标识 | LCD（接口） | 分辨率 | 触摸 | SD 卡 | 音频 |
| --- | --- | --- | --- | --- | --- |
| [jc8048f543](jc8048f543/) | 通用 RGB 面板（RGB） | 800×480 | GT911 | SDMMC | ES8389（DAC + ADC） |
| [jc8048f550](jc8048f550/) | 通用 RGB 面板（RGB） | 800×480 | GT911 | SDMMC | ES8389（DAC + ADC） |
| [jc3636f118](jc3636f118/) | ST77916（QSPI） | 360×360 | CST816S / 无 | SDMMC | — |
| [jc1060f570c](jc1060f570c/) | 通用 RGB 面板（RGB） | 1024×600 | GT911 | SDMMC | ES8389（DAC + ADC） |

`jc8048f543` 与 `jc8048f550` 的屏幕物理尺寸不同，分辨率、引脚、显示时序、触摸、音频、SD 卡和板级默认配置相同。

### ESP32-S3

| 板型标识 | LCD（接口） | 分辨率 | 触摸 | SD 卡 |
| --- | --- | --- | --- | --- |
| [esp32_4827s043](esp32_4827s043/) | ST7262（RGB） | 480×272 | GT911 / XPT2046 / 无 | SPI |
| [esp32_4848s040](esp32_4848s040/) | ST7701（三线 SPI + RGB） | 480×480 | GT911 / 无 | SPI |
| [esp32_8048s043](esp32_8048s043/) | ST7262（RGB） | 800×480 | GT911 / 无 | SPI |
| [esp32_8048s050](esp32_8048s050/) | ST7262（RGB） | 800×480 | GT911 / 无 | SPI |
| [esp32_8048s070](esp32_8048s070/) | ST7262（RGB） | 800×480 | GT911 / 无 | SPI |
| [jc3248w535](jc3248w535/) | AXS15231B（QSPI） | 320×480 | AXS15231B / 无 | SPI |
| [jc3636k518](jc3636k518/) | ST77916（QSPI） | 360×360 | CST816S / 无 | SDMMC |
| [jc3636k718](jc3636k718/) | ST77916（QSPI） | 360×360 | CST816S / 无 | SDMMC |
| [jc3636w518](jc3636w518/) | ST77916（QSPI） | 360×360 | CST816S / 无 | SDMMC |
| [jc4827w543](jc4827w543/) | ST77916（QSPI） | 480×272 | GT911 / XPT2046 / 无 | SPI |
| [jc8048w550](jc8048w550/) | ST7262（RGB） | 800×480 | GT911 / 无 | SPI |

### ESP32

| 板型标识 | LCD（接口） | 分辨率 | 触摸 | SD 卡 |
| --- | --- | --- | --- | --- |
| [esp32_2432s024](esp32_2432s024/) | ST7789（SPI） | 240×320 | CST820 / XPT2046 / 无 | SPI |
| [esp32_2432s028](esp32_2432s028/) | ST7789（SPI） | 240×320 | CST820 / XPT2046 / 无 | SPI |
| [esp32_2432s032](esp32_2432s032/) | ST7796（SPI） | 240×320 | GT911 / XPT2046 / 无 | SPI |
| [esp32_3248s035](esp32_3248s035/) | ST7796（SPI） | 320×480 | GT911 / XPT2046 / 无 | SPI |
| [jc2432w328](jc2432w328/) | ST7789（SPI） | 240×320 | CST820 / XPT2046 / 无 | SPI |

### ESP32-C3

| 板型标识 | LCD（接口） | 分辨率 | 触摸 | SD 卡 |
| --- | --- | --- | --- | --- |
| [esp32_2424s012](esp32_2424s012/) | GC9A01（SPI） | 240×240 | CST816S / 无 | - |

## 快速开始

将本组件复制到应用的 `components/guition_boards`。组件发布到组件中心后，也可通过以下命令添加：

```bash
idf.py add-dependency "guition-jc/guition_boards==0.1.4"
```

本组件已声明 ESP Board Manager 依赖；各板型所需的驱动依赖由 ESP Board Manager 按所选配置生成。

可直接使用本组件的 [board_init 示例](examples/board_init/README.md)。将 `/path/to/esp-idf` 替换为本地 ESP-IDF 6.1 安装目录后执行：

```bash
. /path/to/esp-idf/export.sh
cd examples/board_init
idf.py set-target esp32p4
idf.py bmgr -b jc8012p4a1
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash monitor
```

示例通过相对 `override_path` 引用本组件，初始化 LCD、触摸和背光，不绘制界面，也不初始化音频或 SD 卡。复制示例到其他位置时，需更新 `main/idf_component.yml` 中的本地组件路径。

使用其他开发板时，将目标芯片和板型标识替换为对应值；切换板型后重新生成 BMGR 配置。切换目标芯片时建议使用独立工程目录，避免沿用旧的生成配置。

## 配置与使用说明

- **触摸选择**：ESP32、ESP32-C3 和 ESP32-S3 板型在 menuconfig 的 `Guition touch` 中选择电容、电阻或无触摸，选项由板型能力决定。ESP32-P4 使用各板原生触摸配置。
- **设备初始化**：按名称调用 `esp_board_manager_init_device_by_name()` 初始化 `display_lcd`、`lcd_touch` 和 `lcd_brightness`，具体顺序见示例。多触摸变体板型不要使用 `esp_board_manager_init()` 批量初始化全部外设，以免启用未选择的总线。
- **显示缓冲**：本组件的 ESP32 / ESP32-C3 板型没有 PSRAM，应用需使用内部 RAM 缓冲。JC3248W535 的 AXS15231B 驱动要求整帧提交，应用需配置相应的刷新方式。
- **SD 卡**：支持的板型通过 `GUITION_SD_ENABLED` 启用，默认关闭。通过 ESP Board Manager 的 `fs_sdcard` 设备管理挂载、卸载及卡句柄；参数由板级设备和外设配置提供。共享 SPI 的板型需要应用协调片选状态、LCD / 触摸初始化顺序及挂载期间的触摸轮询。
- **JC4827W543 电阻触摸版 SD 卡**：使用 FAT32 文件系统。
- **P4 配置**：旧版芯片需在 menuconfig 中选择匹配的芯片版本；应用需提供与 Flash 容量匹配的分区表，示例已附带分区表。当前 SD 电源配置保留 LDO4，需按实际硬件确认。

触摸公共接口见 [guition_touch.h](include/guition_touch.h)，板型与触摸变体索引见 [board_catalog.json](board_catalog.json)。

## 许可证

公共适配代码采用 [MIT 许可证](LICENSE)。原板型文件和初始化表保留各自的许可证，详见 [NOTICE](NOTICE) 和 [LICENSES](LICENSES/)。
