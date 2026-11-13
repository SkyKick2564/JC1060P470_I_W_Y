/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "hal/gpio_types.h"
#include "periph_gpio.h"
#include "periph_i2s.h"
#include "periph_ledc.h"
#include "periph_uart.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 7,
    .scl_io_num = 8,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = true,
    },
    .clk_source = I2C_CLK_SRC_DEFAULT,
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_TDM,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
        .tdm = {
            .clk_cfg = {
                .sample_rate_hz = 44100,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_STD_SLOT_BOTH,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = false,
                .big_endian = false,
                .bit_order_lsb = false,
                .skip_mask = false,
                .total_slot = 0,
            },
            .gpio_cfg = {
                .mclk = 13,
                .bclk = 12,
                .ws = 10,
                .dout = 9,
                .din = 48,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_TDM,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
        .tdm = {
            .clk_cfg = {
                .sample_rate_hz = 44100,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_STD_SLOT_BOTH,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = false,
                .big_endian = false,
                .bit_order_lsb = false,
                .skip_mask = false,
                .total_slot = 0,
            },
            .gpio_cfg = {
                .mclk = 13,
                .bclk = 12,
                .ws = 10,
                .dout = 9,
                .din = 48,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_gpio_config_t esp_bmgr_gpio_pa_control_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(11),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 0,
};

const static esp_ldo_channel_config_t esp_bmgr_ldo_mipi_cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
    .flags = {
        .adjustable = 1,
        .owned_by_hw = 0,
    },
};

const static esp_lcd_dsi_bus_config_t esp_bmgr_dsi_display_cfg = {
    .bus_id = 0,
    .num_data_lanes = 2,
    .phy_clk_src = 0,
    .lane_bit_rate_mbps = 750,
    .flags = {
        .clock_lane_force_hs = false,
    },
};

const static periph_ledc_config_t esp_bmgr_ledc_backlight_cfg = {
    .handle = {
        .channel = LEDC_CHANNEL_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    },
    .gpio_num = 23,
    .duty = 0,
    .freq_hz = 4000,
    .duty_resolution = LEDC_TIMER_13_BIT,
    .timer_sel = LEDC_TIMER_1,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .output_invert = false,
};

const static periph_uart_config_t esp_bmgr_uart_1_cfg = {
    .uart_num = 1,
    .rx_buffer_size = 256,
    .tx_buffer_size = 256,
    .use_queue = false,
    .queue_size = 10,
    .intr_alloc_flags = 0,
    .uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0,
        },
    },
    .tx_io_num = 26,
    .rx_io_num = 27,
    .rts_io_num = -1,
    .cts_io_num = -1,
    .uart_mode = UART_MODE_UART,
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "i2s_audio_out",
        .type = "i2s",
        .format = "tdm-out",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_out_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "i2s_audio_in",
        .type = "i2s",
        .format = "tdm-in",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_in_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[4],
        .name = "gpio_pa_control",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_pa_control_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_pa_control_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[5],
        .name = "ldo_mipi",
        .type = "ldo",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_ldo_mipi_cfg,
        .cfg_size = sizeof(esp_bmgr_ldo_mipi_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[6],
        .name = "dsi_display",
        .type = "dsi",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_dsi_display_cfg,
        .cfg_size = sizeof(esp_bmgr_dsi_display_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[7],
        .name = "ledc_backlight",
        .type = "ledc",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_ledc_backlight_cfg,
        .cfg_size = sizeof(esp_bmgr_ledc_backlight_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "uart_1",
        .type = "uart",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_uart_1_cfg,
        .cfg_size = sizeof(esp_bmgr_uart_1_cfg),
        .id = 0,
    },
};
