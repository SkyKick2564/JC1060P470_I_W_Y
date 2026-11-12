/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_camera.h"
#include "dev_display_lcd.h"
#include "dev_fs_fat.h"
#include "dev_lcd_touch.h"
#include "dev_ledc_ctrl.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

// Device configuration structures
const static dev_audio_codec_config_t esp_bmgr_audio_dac_cfg = {
    .name = "audio_dac",
    .chip = "es8311",
    .type = "audio_codec",
    .data_if_type = 0,
    .adc_enabled = false,
    .dac_enabled = true,
    .pa_peripheral = {
        .name = "gpio_pa_control",
        .port = 11,
    },
    .reset_peripheral = {
        .name = NULL,
        .port = -1,
    },
    .i2c_cfg = {
        .name = "i2c_master",
        .port = 0,
        .address = 48,
        .frequency = 400000,
    },
    .i2s_cfg = {
        .name = "i2s_audio_out",
        .port = 0,
        .clk_src = 0,
        .tx_aux_out_io = -1,
        .tx_aux_out_line = 0,
        .tx_aux_out_invert = false,
    },
    .adc_data_cfg = {
        .periph_name = NULL,
        .sample_rate_hz = 0,
        .max_store_buf_size = 0,
        .conv_frame_size = 0,
        .conv_mode = 0,
        .format = 0,
        .pattern_num = 0,
        .cfg_mode = 0,
        .cfg = {
            .single_unit = {
                .unit_id = 0,
                .atten = 0,
                .bit_width = 0,
                .channel_id = {},
            },
        },
    },
    .codec_sys_cfg = {
        .is_master = false,
        .no_mclk = true,
    },
    .codec_adc_cfg = {
        .digital_mic = false,
        .label = "",
    },
    .codec_dac_cfg = {
        .ref_enable = true,
        .ref_dac_ch = 0,
        .real_adc_data_ch = 0,
    },
    .codec_pa_cfg = {
        .pa_pin = 11,
        .pa_active_low = false,
        .hw_gain = {
            .pa_gain = 6.0,
        },
    },
    .codec_reset_cfg = {
        .reset_pin = -1,
        .reset_active_low = true,
    },
    .metadata = NULL,
    .metadata_size = 0,
};

const static dev_audio_codec_config_t esp_bmgr_audio_adc_cfg = {
    .name = "audio_adc",
    .chip = "es8311",
    .type = "audio_codec",
    .data_if_type = 0,
    .adc_enabled = true,
    .dac_enabled = false,
    .pa_peripheral = {
        .name = NULL,
        .port = -1,
    },
    .reset_peripheral = {
        .name = NULL,
        .port = -1,
    },
    .i2c_cfg = {
        .name = "i2c_master",
        .port = 0,
        .address = 48,
        .frequency = 400000,
    },
    .i2s_cfg = {
        .name = "i2s_audio_in",
        .port = 0,
        .clk_src = 0,
        .tx_aux_out_io = -1,
        .tx_aux_out_line = 0,
        .tx_aux_out_invert = false,
    },
    .adc_data_cfg = {
        .periph_name = NULL,
        .sample_rate_hz = 0,
        .max_store_buf_size = 0,
        .conv_frame_size = 0,
        .conv_mode = 0,
        .format = 0,
        .pattern_num = 0,
        .cfg_mode = 0,
        .cfg = {
            .single_unit = {
                .unit_id = 0,
                .atten = 0,
                .bit_width = 0,
                .channel_id = {},
            },
        },
    },
    .codec_sys_cfg = {
        .is_master = false,
        .no_mclk = true,
    },
    .codec_adc_cfg = {
        .digital_mic = false,
        .label = "FC,RE",
    },
    .codec_dac_cfg = {
        .ref_enable = false,
        .ref_dac_ch = 0,
        .real_adc_data_ch = 0,
    },
    .codec_pa_cfg = {
        .pa_pin = -1,
        .pa_active_low = true,
        .hw_gain = {
            .pa_gain = 0.0,
        },
    },
    .codec_reset_cfg = {
        .reset_pin = -1,
        .reset_active_low = true,
    },
    .metadata = NULL,
    .metadata_size = 0,
};

const static dev_display_lcd_config_t esp_bmgr_display_lcd_cfg = {
    .name = "display_lcd",
    .chip = "jd9165",
    .sub_type = "dsi",
    .lcd_width = 1024,
    .lcd_height = 600,
    .swap_xy = false,
    .mirror_x = false,
    .mirror_y = false,
    .need_reset = true,
    .invert_color = false,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    .bits_per_pixel = 24,
    .frame_format = DEV_DISPLAY_LCD_FRAME_FORMAT_RGB565_LE,
    .sub_cfg = {
        .dsi = {
            .dsi_name = "dsi_display",
            .ldo_name = "ldo_mipi",
            .reset_gpio_num = 0,
            .reset_active_high = 0,
            .dbi_config = {
                .virtual_channel = 0,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            },
            .dpi_config = {
                .virtual_channel = 0,
                .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
                .dpi_clock_freq_mhz = 52,
                .in_color_format = LCD_COLOR_FMT_RGB565,
                .out_color_format = LCD_COLOR_FMT_RGB565,
                .num_fbs = 3,
                .flags = {
                    .disable_lp = false,
                },
                .video_timing = {
                    .h_size = 1024,
                    .v_size = 600,
                    .hsync_back_porch = 160,
                    .hsync_pulse_width = 24,
                    .hsync_front_porch = 160,
                    .vsync_back_porch = 21,
                    .vsync_pulse_width = 2,
                    .vsync_front_porch = 12,
                },
            },
            .use_dma2d = true,
        },
    },
};

const static dev_lcd_touch_config_t esp_bmgr_lcd_touch_cfg = {
    .name = "lcd_touch",
    .chip = "gt911",
    .type = "lcd_touch",
    .sub_type = "i2c",
    .touch_config = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    },
    .sub_cfg = {
        .i2c = {
            .i2c_name = "i2c_master",
            .i2c_addr_count = 2,
            .i2c_addr = {0xba, 0x28, 0x00, 0x00},
            .io_i2c_config = {
                .dev_addr = 0,
                .control_phase_bytes = 1,
                .dc_bit_offset = 0,
                .lcd_cmd_bits = 16,
                .lcd_param_bits = 0,
                .scl_speed_hz = 100000,
                .flags = {
                    .dc_low_on_data = false,
                    .disable_control_phase = true,
                },
                .transaction_timeout_ms = 0,
            },
        },
    },
};

const static dev_camera_config_t esp_bmgr_camera_cfg = {
    .name = "camera",
    .type = "camera",
    .sub_type = "csi",
    .sub_cfg = {
        .csi = {
            .i2c_name = "i2c_master",
            .i2c_freq = 100000,
            .ldo_name = "ldo_mipi",
            .reset_io = -1,
            .pwdn_io = -1,
            .dont_init_ldo = true,
            .xclk_config = {
            },
        },
    },
};

const static dev_ledc_ctrl_config_t esp_bmgr_lcd_brightness_cfg = {
    .name = "lcd_brightness",
    .type = "ledc_ctrl",
    .ledc_name = "ledc_backlight",
    .default_percent = 100,
};

const static dev_fs_fat_config_t esp_bmgr_fs_sdcard_cfg = {
    .name = "fs_sdcard",
    .mount_point = "/sdcard",
    .frequency = SDMMC_FREQ_DEFAULT,
    .vfs_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16384,
    },
    .sub_type = "sdmmc",
    .sub_cfg = {
        .sdmmc = {
            .slot = SDMMC_HOST_SLOT_0,
            .bus_width = 4,
            .slot_flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
            .pins = {
                .clk = 43,
                .cmd = 44,
                .d0 = 39,
                .d1 = 40,
                .d2 = 41,
                .d3 = 42,
                .d4 = -1,
                .d5 = -1,
                .d6 = -1,
                .d7 = -1,
                .cd = -1,
                .wp = -1,
            },
            .ldo_chan_id = 4,
        },
    },
};

// Device descriptor array

const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "audio_dac",
        .chip = "es8311",
        .type = "audio_codec",
        .sub_type = NULL,
        .cfg = &esp_bmgr_audio_dac_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_dac_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "audio_adc",
        .chip = "es8311",
        .type = "audio_codec",
        .sub_type = NULL,
        .cfg = &esp_bmgr_audio_adc_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_adc_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[3],
        .name = "display_lcd",
        .chip = "jd9165",
        .type = "display_lcd",
        .sub_type = "dsi",
        .cfg = &esp_bmgr_display_lcd_cfg,
        .cfg_size = sizeof(esp_bmgr_display_lcd_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[4],
        .name = "lcd_touch",
        .chip = "gt911",
        .type = "lcd_touch",
        .sub_type = "i2c",
        .cfg = &esp_bmgr_lcd_touch_cfg,
        .cfg_size = sizeof(esp_bmgr_lcd_touch_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[5],
        .name = "camera",
        .chip = NULL,
        .type = "camera",
        .sub_type = "csi",
        .cfg = &esp_bmgr_camera_cfg,
        .cfg_size = sizeof(esp_bmgr_camera_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[6],
        .name = "lcd_brightness",
        .chip = NULL,
        .type = "ledc_ctrl",
        .sub_type = NULL,
        .cfg = &esp_bmgr_lcd_brightness_cfg,
        .cfg_size = sizeof(esp_bmgr_lcd_brightness_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = NULL,
        .name = "fs_sdcard",
        .chip = NULL,
        .type = "fs_fat",
        .sub_type = "sdmmc",
        .cfg = &esp_bmgr_fs_sdcard_cfg,
        .cfg_size = sizeof(esp_bmgr_fs_sdcard_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
};
