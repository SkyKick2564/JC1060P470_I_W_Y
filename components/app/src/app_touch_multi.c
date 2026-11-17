#include "app_touch_multi.h"

#include <stdbool.h>
#include <stdlib.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "APP_TOUCH_MULTI";

typedef struct app_touch_multi_context app_touch_multi_context_t;

typedef struct {
    app_touch_multi_context_t *context;
    void *original_user_data;
    volatile bool unregistering;
} app_touch_multi_isr_context_t;

typedef struct {
    app_touch_multi_context_t *context;
    lv_indev_t *indev;
    uint8_t index;
    lv_point_t point;
    uint8_t track_id;
    bool track_assigned;
    bool release_pending;
    lv_indev_state_t state;
} app_touch_multi_slot_t;

struct app_touch_multi_context {
    esp_lcd_touch_handle_t handle;
    lv_display_t *display;
    app_touch_multi_slot_t slots[APP_TOUCH_MULTI_POINTERS];
    volatile bool refresh_needed;
    app_touch_multi_isr_context_t *isr_context;
};

static app_touch_multi_context_t *s_touch_context = NULL;

static void app_touch_read(lv_indev_t *indev, lv_indev_data_t *data);
static void app_touch_interrupt_callback(esp_lcd_touch_handle_t tp);
static void app_touch_refresh(app_touch_multi_context_t *context);
static void app_touch_release_all(app_touch_multi_context_t *context);
static int app_touch_find_track(const app_touch_multi_context_t *context, uint8_t track_id);
static int app_touch_find_free_slot(const app_touch_multi_context_t *context);

esp_err_t app_touch_multi_add(lv_display_t *disp, esp_lcd_touch_handle_t handle)
{
    if (s_touch_context != NULL || disp == NULL || handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    app_touch_multi_context_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return ESP_ERR_NO_MEM;
    }

    context->handle = handle;
    context->display = disp;
    context->refresh_needed = true;

    for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
        app_touch_multi_slot_t *slot = &context->slots[i];
        slot->context = context;
        slot->index = i;
        slot->state = LV_INDEV_STATE_RELEASED;

        slot->indev = lv_indev_create();
        if (slot->indev == NULL) {
            ESP_LOGE(TAG, "Failed to create LVGL pointer %u", (unsigned)i);
            for (uint8_t j = 0; j < i; j++) {
                lv_indev_delete(context->slots[j].indev);
            }
            free(context);
            return ESP_ERR_NO_MEM;
        }

        lv_indev_set_type(slot->indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_mode(slot->indev, LV_INDEV_MODE_EVENT);
        lv_indev_set_read_cb(slot->indev, app_touch_read);
        lv_indev_set_disp(slot->indev, disp);
        lv_indev_set_driver_data(slot->indev, slot);
    }

    context->isr_context = calloc(1, sizeof(*context->isr_context));
    if (context->isr_context == NULL) {
        for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
            lv_indev_delete(context->slots[i].indev);
        }
        free(context);
        return ESP_ERR_NO_MEM;
    }

    context->isr_context->context = context;
    context->isr_context->original_user_data = handle->config.user_data;

    esp_err_t err = esp_lcd_touch_register_interrupt_callback_with_data(
        handle,
        app_touch_interrupt_callback,
        context->isr_context
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GT911 interrupt callback: %s",
                 esp_err_to_name(err));
        handle->config.user_data = context->isr_context->original_user_data;
        free(context->isr_context);
        for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
            lv_indev_delete(context->slots[i].indev);
        }
        free(context);
        return err;
    }

    s_touch_context = context;

    ESP_LOGI(TAG, "GT911 multi-touch registered: %u independent pointers",
             (unsigned)APP_TOUCH_MULTI_POINTERS);
    return ESP_OK;
}

esp_err_t app_touch_multi_remove(void)
{
    app_touch_multi_context_t *context = s_touch_context;
    if (context == NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_lcd_touch_register_interrupt_callback(context->handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister GT911 interrupt callback: %s",
                 esp_err_to_name(err));
        return err;
    }

    context->isr_context->unregistering = true;
    __sync_synchronize();
    vTaskDelay(pdMS_TO_TICKS(10));

    context->handle->config.user_data = context->isr_context->original_user_data;
    free(context->isr_context);
    context->isr_context = NULL;

    for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
        if (context->slots[i].indev != NULL) {
            lv_indev_delete(context->slots[i].indev);
            context->slots[i].indev = NULL;
        }
    }

    s_touch_context = NULL;
    free(context);

    ESP_LOGI(TAG, "GT911 multi-touch removed");
    return ESP_OK;
}

static void app_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    app_touch_multi_slot_t *slot =
        (app_touch_multi_slot_t *)lv_indev_get_driver_data(indev);

    if (slot == NULL || slot->context == NULL) {
        data->point.x = 0;
        data->point.y = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    app_touch_multi_context_t *context = slot->context;

    /*
     * The LVGL port wakes all pointer devices for one GT911 interrupt.
     * Read the controller once; all five indevs consume the same snapshot.
     * Clear the flag before reading so a new interrupt during the read is
     * not lost.
     */
    if (slot->index == 0 && context->refresh_needed) {
        context->refresh_needed = false;
        app_touch_refresh(context);
    }

    data->point = slot->point;
    data->state = slot->state;

    /* Keep a released slot reserved until LVGL has received its release. */
    if (slot->release_pending && slot->state == LV_INDEV_STATE_RELEASED) {
        slot->release_pending = false;
    }
}

static void IRAM_ATTR app_touch_interrupt_callback(esp_lcd_touch_handle_t tp)
{
    app_touch_multi_isr_context_t *isr_context =
        (app_touch_multi_isr_context_t *)tp->config.user_data;

    if (isr_context == NULL || isr_context->unregistering ||
            isr_context->context == NULL) {
        return;
    }

    app_touch_multi_context_t *context = isr_context->context;
    context->refresh_needed = true;
    (void)lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, NULL);
}

static void app_touch_refresh(app_touch_multi_context_t *context)
{
    esp_lcd_touch_point_data_t touch_data[APP_TOUCH_MULTI_POINTERS] = {0};
    uint8_t touch_count = 0;

    esp_err_t err = esp_lcd_touch_read_data(context->handle);
    if (err == ESP_OK) {
        err = esp_lcd_touch_get_data(
            context->handle,
            touch_data,
            &touch_count,
            APP_TOUCH_MULTI_POINTERS
        );
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 read failed: %s", esp_err_to_name(err));
        app_touch_release_all(context);
        return;
    }

    /*
     * GT911 reports zero points when its frame contains more than five
     * contacts. Keep the previous pointer states for this frame instead
     * of releasing all active LVGL pointers.
     */
    if (touch_count == 0) {
        return;
    }

    if (touch_count > APP_TOUCH_MULTI_POINTERS) {
        touch_count = APP_TOUCH_MULTI_POINTERS;
    }

    /*
     * Release tracks that disappeared before assigning newly detected
     * tracks. This preserves each finger's pointer identity.
     */
    for (uint8_t slot_index = 0; slot_index < APP_TOUCH_MULTI_POINTERS; slot_index++) {
        app_touch_multi_slot_t *slot = &context->slots[slot_index];
        if (!slot->track_assigned) {
            continue;
        }

        bool found = false;
        for (uint8_t point_index = 0; point_index < touch_count; point_index++) {
            if (touch_data[point_index].track_id == slot->track_id) {
                found = true;
                break;
            }
        }

        if (!found) {
            slot->track_assigned = false;
            slot->release_pending = true;
            slot->state = LV_INDEV_STATE_RELEASED;
        }
    }

    for (uint8_t point_index = 0; point_index < touch_count; point_index++) {
        const esp_lcd_touch_point_data_t *point = &touch_data[point_index];
        int slot_index = app_touch_find_track(context, point->track_id);

        if (slot_index < 0) {
            slot_index = app_touch_find_free_slot(context);
        }

        if (slot_index < 0) {
            ESP_LOGW(TAG, "No free LVGL pointer for GT911 track %u",
                     (unsigned)point->track_id);
            continue;
        }

        app_touch_multi_slot_t *slot = &context->slots[slot_index];
        slot->track_id = point->track_id;
        slot->track_assigned = true;
        slot->point.x = (lv_coord_t)point->x;
        slot->point.y = (lv_coord_t)point->y;
        slot->state = LV_INDEV_STATE_PRESSED;
    }
}

static void app_touch_release_all(app_touch_multi_context_t *context)
{
    for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
        app_touch_multi_slot_t *slot = &context->slots[i];
        if (slot->track_assigned || slot->state == LV_INDEV_STATE_PRESSED) {
            slot->track_assigned = false;
            slot->release_pending = true;
            slot->state = LV_INDEV_STATE_RELEASED;
        }
    }
}

static int app_touch_find_track(const app_touch_multi_context_t *context, uint8_t track_id)
{
    for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
        const app_touch_multi_slot_t *slot = &context->slots[i];
        if (slot->track_assigned && slot->track_id == track_id) {
            return (int)i;
        }
    }

    return -1;
}

static int app_touch_find_free_slot(const app_touch_multi_context_t *context)
{
    for (uint8_t i = 0; i < APP_TOUCH_MULTI_POINTERS; i++) {
        const app_touch_multi_slot_t *slot = &context->slots[i];
        if (!slot->track_assigned && !slot->release_pending) {
            return (int)i;
        }
    }

    return -1;
}
