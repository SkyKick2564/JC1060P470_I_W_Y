#include "app.h"
#include "esp_err.h"
#include "esp_log.h"

#include "ui.h"

#include "esp_board_manager.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"

#include "dev_display_lcd.h"

static const char *TAG = "APP";

esp_err_t app_init(void)
{
    esp_err_t err;

    /* Get LCD handles created by Board Manager */
    dev_display_lcd_handles_t *lcd = NULL;

    err = esp_board_manager_get_device_handle(
            "display_lcd",
            (void **)&lcd);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LCD handles: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (lcd == NULL ||
        lcd->panel_handle == NULL ||
        lcd->io_handle == NULL) {
        ESP_LOGE(TAG, "Invalid LCD handles");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD handles retrieved successfully");
    ESP_LOGI(TAG, "LCD panel: %p", lcd->panel_handle);
    ESP_LOGI(TAG, "LCD IO: %p", lcd->io_handle);

    dev_display_lcd_config_t *lcd_cfg = NULL;

    err = esp_board_manager_get_device_config(
        "display_lcd",
        (void **)&lcd_cfg
    );

    if (err != ESP_OK || lcd_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD config: %s",
                esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD resolution: %lu x %lu",
            (unsigned long)lcd_cfg->lcd_width,
            (unsigned long)lcd_cfg->lcd_height);

    ESP_LOGI(TAG, "LCD frame format: %d", lcd_cfg->frame_format);
    ESP_LOGI(TAG, "LCD subtype: %s", lcd_cfg->sub_type);

    /* Initialize LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    err = lvgl_port_init(&lvgl_cfg);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Configure LVGL display */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd->io_handle,
        .panel_handle = lcd->panel_handle,
        .control_handle = NULL,

        .buffer_size = lcd_cfg->lcd_width * lcd_cfg->lcd_height,
        .double_buffer = true,

        .hres = lcd_cfg->lcd_width,
        .vres = lcd_cfg->lcd_height,

        .monochrome = false,

        .rotation = {
            .swap_xy = lcd_cfg->swap_xy,
            .mirror_x = lcd_cfg->mirror_x,
            .mirror_y = lcd_cfg->mirror_y,
        },

        .color_format = LV_COLOR_FORMAT_RGB888,

        .flags = {
            .buff_spiram = true,
            .buff_dma = false,
            .sw_rotate = false,
            .full_refresh = false,
            .direct_mode = false,
        },
    };

    /* Configure MIPI-DSI display */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,
        },
    };

    lv_display_t *display =
        lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL DSI display");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL display initialized successfully");

    // /* Initialize EEZ UI */
    // ui_init();

    ESP_LOGI(TAG, "Application initialized");

    return ESP_OK;
}