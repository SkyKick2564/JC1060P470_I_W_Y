// SPDX-License-Identifier: MIT
#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

// QSPI 显示 / ST77916
#define LCD_QSPI_HOST               SPI2_HOST
#define LCD_QSPI_TE_PIN             GPIO_NUM_19
#define LCD_QSPI_SCK_PIN            GPIO_NUM_12
#define LCD_QSPI_CS_PIN             GPIO_NUM_13
#define LCD_QSPI_DATA0_PIN          GPIO_NUM_14
#define LCD_QSPI_DATA1_PIN          GPIO_NUM_15
#define LCD_QSPI_DATA2_PIN          GPIO_NUM_16
#define LCD_QSPI_DATA3_PIN          GPIO_NUM_17
#define LCD_RST_PIN                 GPIO_NUM_18
#define LCD_BL_PIN                  GPIO_NUM_5
#define LCD_BL_ON_LEVEL             1

// C 电容触摸 / CST816S
#define TP_C_I2C_HOST               0
#define TP_C_SCL_PIN                GPIO_NUM_10       // I2C SCL
#define TP_C_SDA_PIN                GPIO_NUM_11         // I2C SDA
#define TP_C_RST_PIN                GPIO_NUM_9
#define TP_C_INT_PIN                GPIO_NUM_8

// SD卡 / TF卡
#define SD_SDMMC_SCK_PIN            GPIO_NUM_24
#define SD_SDMMC_CMD_PIN            GPIO_NUM_25
#define SD_SDMMC_D0_PIN             GPIO_NUM_20
#define SD_SDMMC_D1_PIN             GPIO_NUM_21
#define SD_SDMMC_D2_PIN             GPIO_NUM_22
#define SD_SDMMC_D3_PIN             GPIO_NUM_23

// 音频输入 / I2S PDM in
#define I2S_PDM_DATA_PIN            GPIO_NUM_37
#define I2S_PDM_SCK_PIN             GPIO_NUM_39

// 音频输出 / I2S STD out
#define I2S_STD_DAC_LRCK_PIN        GPIO_NUM_51
#define I2S_STD_DAC_DATA_PIN        GPIO_NUM_52
#define I2S_STD_DAC_BCK_PIN         GPIO_NUM_53

// 编码器 / Encoder
#define ENCODER_B_PIN               GPIO_NUM_6
#define ENCODER_A_PIN               GPIO_NUM_7

// 陀螺仪 / I2C 接口， QMI8658C
#define IMU_I2C_SDA_PIN             GPIO_NUM_42
#define IMU_I2C_SCL_PIN             GPIO_NUM_43

// ADC-BAT / 电池电量ADC监测引脚
/*
 *             BAT
 *              |
 *             15K
 *              |
 *  GPIO55---0R--|
 *              |
 *             10K
 *              |
 *             GND
*/
#define BAT_ADC_PIN                 GPIO_NUM_55
