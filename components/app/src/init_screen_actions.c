#include "init_screen_actions.h"

#include "lvgl.h"
#include "ui.h"
#include "esp_lvgl_port.h"

esp_err_t init_screen_actions_init(void)
{
    if (objects.bar_loading == NULL ||
        objects.loading_txt == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

void init_screen_update_progress(
    uint8_t progress,
    const char *message)
{
    if (!lvgl_port_lock(0)) {
        return;
    }

    lv_bar_set_value(
        objects.bar_loading,
        progress,
        LV_ANIM_ON
    );

    lv_label_set_text(
        objects.loading_txt,
        message
    );

    lvgl_port_unlock();
}