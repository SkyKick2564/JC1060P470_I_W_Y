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
    bool long_pressed;
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

    /*
     * Avoid LVGL software image recolor: the renderer was stalling in
     * recolor_only() and starving IDLE0. Keep pressed feedback by fading
     * the image instead of running the recolor path.
     */
    lv_obj_set_style_image_opa(
        image,
        pressed ? LV_OPA_50 : LV_OPA_COVER,
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
    s_icon_drag.long_pressed = false;
    action_icon_set_button_color(lv_event_get_current_target(event), true);
}

static void action_icon_clicked(lv_event_t *event)
{
    /*
     * LVGL sends LV_EVENT_CLICKED on release even after LONG_PRESSED.
     * A long press is the drag gesture, not a normal click.
     */
    if (s_icon_drag.long_pressed) {
        lv_event_stop_processing(event);
    }
}

static void action_icon_long_pressed(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_obj_t *image = lv_obj_get_child(button, 1);
    lv_indev_t *indev;

    if (image == NULL) {
        return;
    }

    /*
     * Recover from a missed release/press-lost event before starting
     * another icon drag. Only one icon can own the drag state at a time.
     */
    if (s_icon_drag.active && s_icon_drag.button != button) {
        action_icon_finish(s_icon_drag.button);
    }

    if (s_icon_drag.active) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }

    s_icon_drag.button = button;
    s_icon_drag.indev = indev;
    s_icon_drag.long_pressed = true;
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
    lv_obj_set_style_transform_scale_x(image, 360, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(image, 360, LV_PART_MAIN);
}

static void action_icon_pressing(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    lv_indev_t *indev;
    lv_point_t point;

    if (!s_icon_drag.active || s_icon_drag.button != button) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev == NULL || indev != s_icon_drag.indev) {
        return;
    }

    lv_indev_get_point(indev, &point);

    /*
     * LV_EVENT_PRESSING is delivered repeatedly while the pointer is down.
     * Do not invalidate and redraw the image on every input poll when the
     * finger has not moved; this was continuously driving the software
     * image renderer (recolor_only) and starving taskLVGL/IDLE0.
     */
    lv_coord_t new_x = s_icon_drag.start_x +
        point.x - s_icon_drag.start_point.x;
    lv_coord_t new_y = s_icon_drag.start_y +
        point.y - s_icon_drag.start_point.y;

    if (lv_obj_get_x(button) != new_x || lv_obj_get_y(button) != new_y) {
        lv_obj_set_pos(button, new_x, new_y);
    }
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
    lv_obj_t *image;

    if (button == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    image = lv_obj_get_child(button, 1);
    if (image == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Remove theme/local styles from the icon image itself. A non-zero
     * image_recolor_opa sends LVGL through recolor_only(), which is where
     * taskLVGL is getting stuck on this target. Re-apply only the normal
     * image opacity; pressed feedback is handled with image opacity too.
     */
    lv_obj_remove_style_all(image);

    /*
     * The button can enter LV_STATE_PRESSED while its child image is
     * rendered. Clear inherited recolor on both objects for every state;
     * otherwise LVGL may still select recolor_only() for the child image.
     */
    lv_obj_set_style_image_recolor_opa(
        button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_ANY
    );
    lv_obj_set_style_image_recolor_opa(
        image, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_ANY
    );
    lv_obj_set_style_image_opa(
        image, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_ANY
    );

    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_add_event_cb(button, action_icon_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(button, action_icon_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(button, action_icon_press_lost, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(button, action_icon_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(button, action_icon_pressing, LV_EVENT_PRESSING, NULL);

    return ESP_OK;
}


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

esp_err_t app_actions_init(void)
{
    esp_err_t err;

    s_icon_drag.button = NULL;
    s_icon_drag.indev = NULL;
    s_icon_drag.active = false;
    s_icon_drag.long_pressed = false;

    s_control_center_drag.target = NULL;
    s_control_center_drag.indev = NULL;
    s_control_center_drag.active = false;
    s_control_center_opa_initialized = false;

    err = action_icon_attach(objects.camera_btn);
    if (err != ESP_OK) return err;
    err = action_icon_attach(objects.media_btn);
    if (err != ESP_OK) return err;
    err = action_icon_attach(objects.setting_btn);
    if (err != ESP_OK) return err;

    err = action_control_center_attach(objects.control_center_overlay);
    if (err != ESP_OK) return err;
    err = action_control_center_attach(objects.control_center_touch_area);
    if (err != ESP_OK) return err;

    return ESP_OK;
}
