#include "app.h"
#include "app_context.h"
#include "app_config.h"

#include "esp_err.h"
#include "esp_log.h"

#include "app_touch_multi.h"
#include "actions.h"
#include "init_screen_actions.h"
#include "home_actions.h"
#include "control_center_actions.h"

#include "ui.h"

#include "esp_board_manager.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "esp_lcd_mipi_dsi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"

#include "esp_heap_caps.h"

static const char *TAG = "APP";

static app_context_t app_context;
static esp_err_t app_lvgl_deinit(app_context_t *ctx);

static esp_err_t app_before_update(void *user_ctx)
{
    esp_err_t err;
    app_context_t *app_ctx = (app_context_t *)user_ctx;

    ESP_LOGW(TAG, "OTA: Application pre-flash callback");

    /* Deinit performs STOP internally, then tears down Wi-Fi and ESP-Hosted. */
    err = network_manager_deinit(app_ctx->network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network Manager deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Network Manager and ESP-Hosted deinitialized");

    /* LVGL can be deinitialized only after all media tasks have exited. */
    err = app_lvgl_deinit(app_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_board_manager_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board Manager deinit failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Board Manager deinitialized");
    return ESP_OK;
}

static void app_update_available(
    const ota_firmware_info_t *info,
    void *user_ctx)
{
    (void)user_ctx;

    ESP_LOGW(TAG, "OTA: Update available");
    ESP_LOGI(TAG, "  Version: %s", info->version);
    ESP_LOGI(TAG, "  Description: %s",
             info->description[0] != '\0' ? info->description : "(none)");
    ESP_LOGI(TAG, "  Release date: %s",
             info->release_date[0] != '\0' ? info->release_date : "(none)");
    ESP_LOGI(TAG, "  Size: %u bytes", (unsigned)info->size);
}

// static void show_settings_event(lv_event_t *event)
// {
//     lv_obj_add_flag(objects.home, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_remove_flag(objects.setting, LV_OBJ_FLAG_HIDDEN);
// }

// static void show_home_event(lv_event_t *event)
// {
//     lv_obj_add_flag(objects.setting, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_remove_flag(objects.home, LV_OBJ_FLAG_HIDDEN);
// }

static esp_err_t unregister_dsi_callbacks(app_context_t *ctx)
{
    esp_err_t err;

    if (ctx->panel == NULL) {
        return ESP_OK;
    }

    const esp_lcd_dpi_panel_event_callbacks_t empty_callbacks = {0};

    err = esp_lcd_dpi_panel_register_event_callbacks(
        ctx->panel, &empty_callbacks, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister DSI callbacks: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DSI callbacks unregistered");
    return ESP_OK;
}

static esp_err_t app_remove_touch_multi(void)
{
    esp_err_t err;
    err = app_touch_multi_remove();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove multi-touch: %s",
            esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Multi-touch removed");
    return ESP_OK;
}

static esp_err_t app_remove_disp(app_context_t *ctx)
{
    esp_err_t err;

    if (ctx->display == NULL) {
        return ESP_OK;
    }

    err = lvgl_port_remove_disp(ctx->display);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove display: %s",
            esp_err_to_name(err));
        return err;
    }
    ctx->display = NULL;
    ctx->panel = NULL;

    ESP_LOGI(TAG, "Display removed");
    return ESP_OK;
}

static esp_err_t app_lvgl_init(app_context_t *ctx)
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
    lvgl_cfg.task_affinity = 0;
    lvgl_cfg.task_priority = 5;
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
            .direct_mode = true,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true,
        },
    };

    /*
     * Lock LVGL before adding the display.
     * This prevents the LVGL task from rendering
     * the first frame before the background is set.
     */
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        lvgl_port_stop();
        lvgl_port_deinit();
        return ESP_FAIL;
    }
    
    ctx->display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ctx->panel = lcd->panel_handle;
    if (ctx->display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL DSI display");
        lvgl_port_unlock();
        lvgl_port_stop();
        lvgl_port_deinit();
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
                esp_err_to_name(err != ESP_OK
                    ? err : ESP_ERR_INVALID_STATE));

        esp_err_t cleanup_err = unregister_dsi_callbacks(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unregister DSI callbacks: %s",
                    esp_err_to_name(cleanup_err));
        }
        cleanup_err = app_remove_disp(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove display: %s",
                     esp_err_to_name(cleanup_err));
        }
        lvgl_port_unlock();
        lvgl_port_stop();
        lvgl_port_deinit();
        return err != ESP_OK? err : ESP_ERR_INVALID_STATE;
    }

    /* Register GT911 as five independent LVGL pointer devices */
    err = app_touch_multi_add(ctx->display, touch->touch_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add five-point LVGL Touch: %s",
                 esp_err_to_name(err));
        esp_err_t cleanup_err = unregister_dsi_callbacks(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unregister DSI callbacks: %s",
                    esp_err_to_name(cleanup_err));
        }
        cleanup_err = app_remove_disp(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove display: %s",
                     esp_err_to_name(cleanup_err));
        }
        lvgl_port_unlock();
        lvgl_port_stop();
        lvgl_port_deinit();
        return err;
    }
    ESP_LOGI(TAG, "Five-point LVGL Touch initialized successfully");

    // /* Set initial screen background before first refresh */
    // lv_obj_t *screen = lv_display_get_screen_active(ctx->display);

    // lv_obj_set_style_bg_color(
    //     screen,
    //     lv_color_hex(0x000000),
    //     LV_PART_MAIN
    // );

    // lv_obj_set_style_bg_opa(
    //     screen,
    //     LV_OPA_COVER,
    //     LV_PART_MAIN
    // );

    // /* Initialize EEZ UI */
    ui_init();

    // 3. Initialize App Actions
    err = app_actions_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "App Actions init failed: %s",
             esp_err_to_name(err));
        esp_err_t cleanup_err = unregister_dsi_callbacks(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unregister DSI callbacks: %s",
                    esp_err_to_name(cleanup_err));
        }
        cleanup_err = app_remove_disp(ctx);
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove display: %s",
                    esp_err_to_name(cleanup_err));
        }
        lvgl_port_unlock();
        lvgl_port_stop();
        lvgl_port_deinit();
        return err;
    }

    /* Allow LVGL task to start rendering */
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Application initialized");

    return ESP_OK;
}

static esp_err_t app_lvgl_deinit(app_context_t *ctx)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Deinitializing LVGL...");

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    /* Unregister DSI panel callbacks */
    err = unregister_dsi_callbacks(ctx);
    if (err != ESP_OK) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Failed to unregister DSI callbacks: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Remove all five LVGL touch pointers */
    err = app_remove_touch_multi();
    if (err != ESP_OK) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Failed to remove multi-touch: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = app_remove_disp(ctx);
    if (err != ESP_OK) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Failed to remove display: %s",
                 esp_err_to_name(err));
        return err;
    }

    lvgl_port_unlock();

    /* Stop LVGL timer */
    err = lvgl_port_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop LVGL timer: %s",
                 esp_err_to_name(err));
        return err;
    }

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

static esp_err_t app_process_request(
    app_context_t *ctx,
    const app_request_t *request)
{
    switch (request->type)
    {
    case APP_REQUEST_STARTUP:
        esp_err_t err = app_lvgl_init(ctx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LVGL init failed: %s",
                    esp_err_to_name(err));
            return err;
        }

        err = network_manager_init(&ctx->network);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Network Manager init failed: %s",
                    esp_err_to_name(err));
        }else{
            ESP_LOGI(TAG, "Network Manager initialized");   
        }

        init_screen_update_progress(50,"Network Manager initialized..");

        err = network_manager_start(ctx->network);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Network Manager start failed: %s",
                    esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Network Manager started");
        }

        init_screen_update_progress(80,"Network Manager started..");

        ota_manager_config_t ota_config = APP_OTA_CONFIG;
        ota_config.network = ctx->network;
        ota_config.user_ctx = ctx;
        err = ota_manager_init(
            &ctx->ota,
            &ota_config
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA Manager init failed; networking will continue: %s",
                    esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "OTA Manager initialized");
        }
        
        init_screen_update_progress(100,"OTA Manager initialized");
        /*
        * Confirm a pending OTA image as soon as the board and essential UI
        * initialization have succeeded. Do not make rollback confirmation
        * depend on optional SD/media playback or later network startup.
        */
        ota_manager_confirm_running_firmware();

        break;
    case APP_REQUEST_COMPONENT_READY:
        break;
    case APP_REQUEST_COMPONENT_ERROR:
        break;
    case APP_REQUEST_SHOW_SCREEN:
        break;
    case APP_REQUEST_SHUTDOWN:
        break;
    default:
        ESP_LOGW(TAG, "Unknown App Request: %d", request->type);
        return ESP_ERR_INVALID_ARG;
        break;
    }
    return ESP_OK;
}

static void app_main_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    app_request_t request;

    for(;;){
        if (xQueueReceive(ctx->queue, &request, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = app_process_request(ctx, &request);
            if(err != ESP_OK){
                ESP_LOGE(TAG, "App Task stopped due to error");
                return;
            }
        }
    }
}

esp_err_t app_init(void)
{
    // esp_err_t err = app_lvgl_init(&app_context);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "LVGL init failed: %s",
    //              esp_err_to_name(err));
    //     return err;
    // }
    
    // err = network_manager_init(&app_context.network);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Network Manager init failed: %s",
    //              esp_err_to_name(err));
    // }else{
    //     ESP_LOGI(TAG, "Network Manager initialized");   
    // }
    memset(&app_context, 0, sizeof(app_context));

    app_context.queue = xQueueCreateStatic(
        APP_QUEUE_LENGTH,
        sizeof(app_request_t),
        app_context.queue_storage,
        &app_context.queue_buffer
    );
    if (app_context.queue == NULL) {
        ESP_LOGE(TAG, "Failed to create App Queue");
        return ESP_ERR_NO_MEM;
    }

    app_context.task = xTaskCreateStatic(
        app_main_task,
        "app_task",
        APP_TASK_STACK_DEPTH,
        &app_context,
        APP_TASK_PRIORITY,
        app_context.task_stack,
        &app_context.task_buffer
    );
    if (app_context.task == NULL) {
        ESP_LOGE(TAG, "Failed to create App Task");
        return ESP_ERR_NO_MEM;
    }
    /* Send Startup Request */
    app_request_t request = {
        .type = APP_REQUEST_STARTUP
    };

    if (xQueueSend(app_context.queue, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send App Startup Request");
        return ESP_FAIL;
    }
    return ESP_OK;
}