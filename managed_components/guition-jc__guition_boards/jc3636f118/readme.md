# JC3636F118

ESP32-S31，360×360 ST77916 QSPI 屏幕，CST816S 电容触摸。默认启用电容触摸，可在 menuconfig 选择 `No touch`；无电阻触摸配置。

| 外设 | GPIO |
| --- | --- |
| LCD SCK / CS | 12 / 13 |
| LCD DATA0 / DATA1 / DATA2 / DATA3 | 14 / 15 / 16 / 17 |
| LCD RESET / 背光 | 18 / 5 |
| 触摸 SCL / SDA / RESET / INT | 10 / 11 / 9 / 8 |
| SDMMC CLK / CMD / D0 / D1 / D2 / D3 | 24 / 25 / 20 / 21 / 22 / 23 |

LCD TE 接线为 GPIO19，默认不启用 TE 同步。SDMMC 使用 4 位总线，未配置片内 LDO 电源控制。

CPU 默认 320 MHz，Flash 默认 16 MB、QIO 80 MHz，八线 PSRAM 默认 200 MHz。音频、编码器、IMU 及电池 ADC 引脚定义位于 `board_pins.h`，不自动初始化这些外设。
