#include "actions.h"

#include <stdbool.h>

#include "lvgl.h"
#include "ui.h"

static struct {
    lv_obj_t *button;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_coord_t start_x;
    lv_coord_t start_y;
    bool active;
} s_icon_drag;

static void action_icon_set_button_color(lv_obj_t *button, bool pressed)
{
    lv_obj_t *label;
    lv_obj_t *image;

    if (button == NULL) {
        return;
    }

    label = lv_obj_get_child(button, 0);
    image = lv_obj_get_child(button, 1);

    if (label == NULL || image == NULL) {
        return;
    }

    lv_obj_set_style_text_color(
        label,
        pressed ? lv_color_hex(0x6e6e6e) : lv_color_hex(0xffffff),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor(image, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(
        image,
        pressed ? LV_OPA_50 : LV_OPA_TRANSP,
        LV_PART_MAIN
    );
}

static void action_icon_finish(lv_obj_t *button)
{
    lv_obj_t *image;

    action_icon_set_button_color(button, false);

    if (!s_icon_drag.active || s_icon_drag.button != button) {
        return;
    }

    image = lv_obj_get_child(button, 1);
    if (image != NULL) {
        lv_obj_set_style_transform_scale_x(image, 256, LV_PART_MAIN);
        lv_obj_set_style_transform_scale_y(image, 256, LV_PART_MAIN);
    }

    lv_obj_set_pos(button, s_icon_drag.start_x, s_icon_drag.start_y);

    s_icon_drag.active = false;
    s_icon_drag.button = NULL;
    s_icon_drag.indev = NULL;
}

static void action_icon_pressed(lv_event_t *event)
{
    action_icon_set_button_color(lv_event_get_current_target(event), true);
}

static void action_icon_long_pressed(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_obj_t *image = lv_obj_get_child(button, 1);
    lv_indev_t *indev;

    if (s_icon_drag.active || image == NULL) {
        return;
    }

    indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    s_icon_drag.button = button;
    s_icon_drag.indev = indev;
    s_icon_drag.start_x = lv_obj_get_x(button);
    s_icon_drag.start_y = lv_obj_get_y(button);
    lv_indev_get_point(indev, &s_icon_drag.start_point);
    s_icon_drag.active = true;

    lv_obj_move_foreground(button);

    lv_obj_set_style_transform_pivot_x(
        image, lv_obj_get_width(image) / 2, LV_PART_MAIN
    );
    lv_obj_set_style_transform_pivot_y(
        image, lv_obj_get_height(image) / 2, LV_PART_MAIN
    );
    lv_obj_set_style_transform_scale_x(image, 282, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(image, 282, LV_PART_MAIN);
}

static void action_icon_pressing(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev;
    lv_point_t point;

    if (!s_icon_drag.active || s_icon_drag.button != button) {
        return;
    }

    indev = lv_indev_active();
    if (indev == NULL || indev != s_icon_drag.indev) {
        return;
    }

    lv_indev_get_point(indev, &point);

    lv_obj_set_pos(
        button,
        s_icon_drag.start_x + point.x - s_icon_drag.start_point.x,
        s_icon_drag.start_y + point.y - s_icon_drag.start_point.y
    );
}

static void action_icon_released(lv_event_t *event)
{
    action_icon_finish(lv_event_get_current_target(event));
}

static void action_icon_press_lost(lv_event_t *event)
{
    action_icon_finish(lv_event_get_current_target(event));
}

static esp_err_t action_icon_attach(lv_obj_t *button)
{
    if (button == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_add_event_cb(button, action_icon_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(button, action_icon_press_lost, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(button, action_icon_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_pressing, LV_EVENT_PRESSING, NULL);

    return ESP_OK;
}

esp_err_t app_actions_init(void)
{
    esp_err_t err;

    s_icon_drag.button = NULL;
    s_icon_drag.indev = NULL;
    s_icon_drag.active = false;

    err = action_icon_attach(objects.camera_btn);
    if (err != ESP_OK) {
        return err;
    }

    err = action_icon_attach(objects.media_btn);
    if (err != ESP_OK) {
        return err;
    }

    err = action_icon_attach(objects.setting_btn);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}
