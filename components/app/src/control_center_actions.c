#include "control_center_actions.h"

#include <stdbool.h>
#include <stdlib.h>

#include "lvgl.h"
#include "ui.h"

/* Control Center drag and snap behavior */
#define CONTROL_CENTER_HIDDEN_Y       (-600)
#define CONTROL_CENTER_OPEN_Y         (0)
#define CONTROL_CENTER_ANIM_SPEED     (2000)
#define CONTROL_CENTER_FLICK_SPEED    (650)
#define CONTROL_CENTER_FLICK_MIN_MOVE (18)

static struct {
    lv_obj_t *target;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_point_t last_point;
    lv_coord_t start_y;
    uint32_t last_tick;
    int32_t velocity_y;
    bool active;
} s_control_center_drag;

static lv_opa_t s_control_center_max_opa = LV_OPA_COVER;
static bool s_control_center_opa_initialized;

static void action_control_center_init_opacity(void)
{
    if (s_control_center_opa_initialized ||
        objects.control_center_overlay == NULL) {
        return;
    }
    s_control_center_max_opa = lv_obj_get_style_bg_opa(
        objects.control_center_overlay, LV_PART_MAIN);
    s_control_center_opa_initialized = true;
}

static void action_control_center_update_opacity(lv_coord_t y)
{
    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL) return;

    action_control_center_init_opacity();

    int32_t progress = y - CONTROL_CENTER_HIDDEN_Y;
    if (progress < 0) progress = 0;
    else if (progress > CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y)
        progress = CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y;

    int32_t opacity = ((int32_t)s_control_center_max_opa * progress) /
        (CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y);
    lv_obj_set_style_bg_opa(overlay, (lv_opa_t)opacity, LV_PART_MAIN);
}

static bool action_control_center_is_event_target(lv_obj_t *target)
{
    return target != NULL &&
        (target == objects.control_center_overlay ||
         target == objects.control_center_touch_area);
}

static void action_control_center_anim_exec(void *obj, int32_t y)
{
    lv_obj_t *overlay = (lv_obj_t *)obj;
    lv_obj_set_y(overlay, (lv_coord_t)y);
    action_control_center_update_opacity((lv_coord_t)y);
}

static void action_control_center_animate(lv_coord_t target_y)
{
    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL) return;

    lv_coord_t current_y = lv_obj_get_y(overlay);
    int32_t distance = target_y - current_y;
    lv_anim_delete(overlay, action_control_center_anim_exec);

    if (distance == 0) {
        action_control_center_update_opacity(target_y);
        return;
    }

    int32_t abs_distance = distance >= 0 ? distance : -distance;
    int32_t duration = (abs_distance * 1000) / CONTROL_CENTER_ANIM_SPEED;
    if (duration < 1) duration = 1;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlay);
    lv_anim_set_exec_cb(&anim, action_control_center_anim_exec);
    lv_anim_set_values(&anim, current_y, target_y);
    lv_anim_set_duration(&anim, (uint32_t)duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

static void action_control_center_update_velocity(lv_point_t point, uint32_t now)
{
    uint32_t elapsed = now - s_control_center_drag.last_tick;
    if (elapsed > 0) {
        int32_t delta_y = point.y - s_control_center_drag.last_point.y;
        int32_t new_velocity = (delta_y * 1000) / (int32_t)elapsed;
        s_control_center_drag.velocity_y =
            (s_control_center_drag.velocity_y * 2 + new_velocity) / 3;
    }
    s_control_center_drag.last_point = point;
    s_control_center_drag.last_tick = now;
}

static void action_control_center_pressed(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    if (objects.control_center_overlay == NULL ||
        !action_control_center_is_event_target(target) ||
        s_control_center_drag.active) return;

    action_control_center_init_opacity();
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;

    lv_anim_delete(objects.control_center_overlay, action_control_center_anim_exec);
    s_control_center_drag.target = target;
    s_control_center_drag.indev = indev;
    s_control_center_drag.start_y = lv_obj_get_y(objects.control_center_overlay);
    lv_indev_get_point(indev, &s_control_center_drag.start_point);
    s_control_center_drag.last_point = s_control_center_drag.start_point;
    s_control_center_drag.last_tick = lv_tick_get();
    s_control_center_drag.velocity_y = 0;
    s_control_center_drag.active = true;
    lv_obj_move_foreground(objects.control_center_overlay);
}

static void action_control_center_pressing(lv_event_t *event)
{
    if (!s_control_center_drag.active ||
        !action_control_center_is_event_target(lv_event_get_target(event))) return;

    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL || indev != s_control_center_drag.indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    action_control_center_update_velocity(point, lv_tick_get());

    lv_coord_t new_y = s_control_center_drag.start_y +
        point.y - s_control_center_drag.start_point.y;
    if (new_y < CONTROL_CENTER_HIDDEN_Y) new_y = CONTROL_CENTER_HIDDEN_Y;
    else if (new_y > CONTROL_CENTER_OPEN_Y) new_y = CONTROL_CENTER_OPEN_Y;

    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay != NULL && lv_obj_get_y(overlay) != new_y) {
        lv_obj_set_y(overlay, new_y);
        action_control_center_update_opacity(new_y);
    }
}

static void action_control_center_finish(lv_event_t *event)
{
    if (!s_control_center_drag.active ||
        !action_control_center_is_event_target(lv_event_get_target(event))) return;

    lv_obj_t *overlay = objects.control_center_overlay;
    if (overlay == NULL || s_control_center_drag.indev == NULL) {
        s_control_center_drag.active = false;
        s_widget_drag.widget = NULL;
    s_widget_drag.indev = NULL;
    s_widget_drag.active = false;

    s_control_center_drag.target = NULL;
        s_control_center_drag.indev = NULL;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(s_control_center_drag.indev, &point);
    action_control_center_update_velocity(point, lv_tick_get());

    lv_coord_t current_y = lv_obj_get_y(overlay);
    int32_t total_move = point.y - s_control_center_drag.start_point.y;
    int32_t velocity_y = s_control_center_drag.velocity_y;

    bool fast_down = total_move >= CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y >= CONTROL_CENTER_FLICK_SPEED;
    bool fast_up = total_move <= -CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y <= -CONTROL_CENTER_FLICK_SPEED;

    lv_coord_t destination;
    if (fast_down) destination = CONTROL_CENTER_OPEN_Y;
    else if (fast_up) destination = CONTROL_CENTER_HIDDEN_Y;
    else destination = current_y > (CONTROL_CENTER_HIDDEN_Y / 2)
        ? CONTROL_CENTER_OPEN_Y : CONTROL_CENTER_HIDDEN_Y;

    s_control_center_drag.active = false;
    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.velocity_y = 0;
    action_control_center_animate(destination);
}

static void action_control_center_released(lv_event_t *event)
{
    action_control_center_finish(event);
}

static void action_control_center_press_lost(lv_event_t *event)
{
    action_control_center_finish(event);
}

static esp_err_t action_control_center_attach(lv_obj_t *object)
{
    if (object == NULL) return ESP_ERR_INVALID_STATE;

    lv_obj_add_flag(object, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(object, action_control_center_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(object, action_control_center_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(object, action_control_center_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(object, action_control_center_press_lost, LV_EVENT_PRESS_LOST, NULL);
    return ESP_OK;
}

esp_err_t control_center_actions_init(void)
{
    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.active = false;
    s_control_center_opa_initialized = false;

    for (size_t i = 0; i < s_app_icon_count; i++) {
        err = action_icon_attach(s_app_icons[i].button);
        if (err != ESP_OK) return err;
    }

    for (size_t i = 0; i < s_home_widget_count; i++) {
        err = action_widget_drag_attach(s_home_widgets[i].object);
        if (err != ESP_OK) return err;
    }

    err = action_control_center_attach(objects.control_center_overlay);
    if (err != ESP_OK) return err;
    err = action_control_center_attach(objects.control_center_touch_area);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
