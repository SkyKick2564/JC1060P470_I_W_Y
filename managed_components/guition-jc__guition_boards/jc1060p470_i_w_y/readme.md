# JC1060P470（带外壳）

BMGR 板型 `jc1060p470_i_w_y` 对应 `JC1060P470C_I_W_Y`，使用 JD9165 显示控制器和 GT911 触摸控制器。LCD 复位为 GPIO0，背光为 GPIO23，SDMMC 电源使用 LDO4。

运行 `idf.py bmgr -b jc1060p470_i_w_y` 选择板型，然后在
`idf.py menuconfig → Guition JC1060P470 display → LCD variant` 中选择屏幕。

| 屏幕 | 分辨率 | DPI 时钟 | 水平后沿 / 同步脉宽 / 前沿 | 垂直后沿 / 同步脉宽 / 前沿 |
| --- | --- | --- | --- | --- |
| LCD V1 | 1024×600 | 52MHz | 160 / 24 / 160 | 21 / 2 / 12 |
| LCD V2（默认） | 1024×600 | 52MHz | 136 / 24 / 160 | 21 / 2 / 12 |

两种屏幕使用各自的初始化命令，RGB565 像素格式、帧缓冲数量及触摸配置由板级配置提供。V2 的水平后沿在创建 panel 时设为 136；帧缓冲数量沿用 `board_devices.yaml` 中的 `num_fbs`。

带外壳版本具有 IP101；当前 BMGR 0.7.3~1 没有原生 IP101 设备支持，
按用户要求不添加以太网设备、驱动依赖或初始化代码。
`board_pins.h` 仅保留用户文档中已有的以太网引脚，供后续参考：

| 信号 | GPIO |
| --- | --- |
| REF_CLK | 50 |
| TX_EN / TXD0 / TXD1 | 49 / 34 / 35 |
| CRS_DV / RXD0 / RXD1 | 28 / 29 / 30 |
| PHY reset | 51 |
| MDC / MDIO | 31 / 52 |

这些参考宏不会初始化以太网硬件。
