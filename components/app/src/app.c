#include "app.h"
#include "esp_err.h"
#include "esp_log.h"

#include "ui.h"

#include "esp_board_manager.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"

#include "dev_display_lcd.h"

static const char *TAG = "APP";

#define WIFI_ICON_ARC_COUNT 3
#define WIFI_ICON_SIZE 32
#define WIFI_ICON_RIGHT 16
#define WIFI_ICON_TOP 10

static lv_obj_t *s_wifi_arcs[WIFI_ICON_ARC_COUNT];
static lv_obj_t *s_wifi_dot;

static void app_create_wifi_icon(void)
{
    if (objects.main == NULL || s_wifi_dot != NULL) {
        return;
    }

    const lv_coord_t sizes[WIFI_ICON_ARC_COUNT] = {32, 24, 16};
    const lv_coord_t widths[WIFI_ICON_ARC_COUNT] = {3, 3, 2};
    const lv_coord_t x = 1024 - WIFI_ICON_RIGHT - WIFI_ICON_SIZE;

    for (int i = 0; i < WIFI_ICON_ARC_COUNT; ++i) {
        lv_obj_t *arc = lv_arc_create(objects.main);
        s_wifi_arcs[i] = arc;
        lv_obj_set_size(arc, sizes[i], sizes[i]);
        lv_obj_set_pos(arc,
            x + (WIFI_ICON_SIZE - sizes[i]) / 2,
            WIFI_ICON_TOP + (WIFI_ICON_SIZE - sizes[i]));
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_arc_set_value(arc, 0);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, widths[i], LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x777777), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    s_wifi_dot = lv_obj_create(objects.main);
    lv_obj_remove_style_all(s_wifi_dot);
    lv_obj_set_size(s_wifi_dot, 5, 5);
    lv_obj_set_pos(s_wifi_dot, x + 13, WIFI_ICON_TOP + 27);
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, lv_color_hex(0x777777), 0);
    lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_40, 0);
    lv_obj_clear_flag(s_wifi_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

void app_set_wifi_status(bool connected, bool rssi_valid, int8_t rssi)
{
    if (s_wifi_dot == NULL || !lvgl_port_lock(1000)) {
        return;
    }

    int signal_level = 0;
    if (connected) {
        if (!rssi_valid) signal_level = 3;
        else if (rssi >= -55) signal_level = 4;
        else if (rssi >= -67) signal_level = 3;
        else if (rssi >= -75) signal_level = 2;
        else if (rssi >= -85) signal_level = 1;
    }

    int visible_arcs = signal_level > 0 ? signal_level - 1 : 0;
    lv_color_t color = connected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x777777);

    for (int i = 0; i < WIFI_ICON_ARC_COUNT; ++i) {
        lv_obj_set_style_arc_color(s_wifi_arcs[i], color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(s_wifi_arcs[i],
            connected && i < visible_arcs ? LV_OPA_COVER : LV_OPA_20,
            LV_PART_MAIN);
    }

    lv_obj_set_style_bg_color(s_wifi_dot, color, 0);
    lv_obj_set_style_bg_opa(s_wifi_dot, connected ? LV_OPA_COVER : LV_OPA_40, 0);
    lvgl_port_unlock();
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

    /*
     * Lock LVGL before adding the display.
     * This prevents the LVGL task from rendering
     * the first frame before the background is set.
     */
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    lv_display_t *display =
        lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL DSI display");
        lvgl_port_unlock();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL display initialized successfully");

    /* Set initial screen background before first refresh */
    lv_obj_t *screen = lv_display_get_screen_active(display);

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
    app_create_wifi_icon();

    /* Allow LVGL task to start rendering */
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Application initialized");

    return ESP_OK;
}