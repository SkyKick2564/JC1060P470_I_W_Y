#include "app.h"
#include "app_touch_multi.h"
#include "esp_err.h"
#include "esp_log.h"

#include "ui.h"

#include "esp_board_manager.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"

static const char *TAG = "APP";

static lv_display_t *s_display = NULL;

static void show_settings_event(lv_event_t *event)
{
    lv_obj_add_flag(objects.home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(objects.setting, LV_OBJ_FLAG_HIDDEN);
}

static void show_home_event(lv_event_t *event)
{
    lv_obj_add_flag(objects.setting, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(objects.home, LV_OBJ_FLAG_HIDDEN);
}

esp_err_t app_lvgl_deinit(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Deinitializing LVGL...");

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    /* Remove all five LVGL touch pointers before deinitializing GT911 */
    err = app_touch_multi_remove();
    if (err != ESP_OK) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Failed to remove multi-touch: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Remove display from LVGL */
    if (s_display != NULL) {
        err = lvgl_port_remove_disp(s_display);
        if (err != ESP_OK) {
            lvgl_port_unlock();
            ESP_LOGE(TAG, "Failed to remove display: %s",
                     esp_err_to_name(err));
            return err;
        }

        s_display = NULL;
        ESP_LOGI(TAG, "Display removed");
    }

    lvgl_port_unlock();

    /* Stop LVGL task and deinitialize LVGL */
    err = lvgl_port_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LVGL deinitialized");

    return ESP_OK;
}

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
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    /*
     * Keep LVGL/UI refresh on Core 0. Media extraction, decode and video
     * rendering are pinned to Core 1 in media_manager_video_player.c.
     */
    lvgl_cfg.task_affinity = 0;
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

    /*
     * Lock LVGL before adding the display.
     * This prevents the LVGL task from rendering
     * the first frame before the background is set.
     */
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    s_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    if (s_display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL DSI display");
        lvgl_port_unlock();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL display initialized successfully");

    /* Get Touch device handle from Board Manager */
    dev_lcd_touch_handles_t *touch = NULL;
    err = esp_board_manager_get_device_handle(
        "lcd_touch",
        (void **)&touch
    );

    if (err != ESP_OK || touch == NULL ||
        touch->touch_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get Touch handle: %s",
                esp_err_to_name(err));
        lvgl_port_unlock();
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }

    /* Register GT911 as five independent LVGL pointer devices */
    err = app_touch_multi_add(s_display, touch->touch_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add five-point LVGL Touch: %s",
                 esp_err_to_name(err));
        lvgl_port_unlock();
        return err;
    }

    ESP_LOGI(TAG, "Five-point LVGL Touch initialized successfully");

    /* Set initial screen background before first refresh */
    lv_obj_t *screen = lv_display_get_screen_active(s_display);

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(0x000000),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    // /* Initialize EEZ UI */
    ui_init();

    // Settings button
    lv_obj_add_event_cb(
        objects.setting_btn,
        show_settings_event,
        LV_EVENT_CLICKED,
        NULL
    );

    lv_obj_add_event_cb(
        objects.back_setting_btn,
        show_home_event,
        LV_EVENT_CLICKED,
        NULL
    );
    /* Allow LVGL task to start rendering */
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Application initialized");

    return ESP_OK;
}