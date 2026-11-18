#include "actions.h"
#include "screens.h"

/*
 * ESP32-P4:
 * ใช้ Radial Gradient จริงของ LVGL 9.5
 */
void action_set_radial_background(lv_event_t *e)
{
    (void)e;

    if (objects.main == NULL) {
        return;
    }

    static lv_style_t style;
    static lv_grad_dsc_t grad;
    static bool initialized = false;

    if (!initialized) {
        static const lv_color_t colors[] = {
            LV_COLOR_MAKE(0x77, 0x77, 0x77),
            LV_COLOR_MAKE(0x20, 0x20, 0x20)
        };

        lv_style_init(&style);

        lv_grad_init_stops(
            &grad,
            colors,
            NULL,
            NULL,
            2
        );

        lv_grad_radial_init(
            &grad,
            LV_GRAD_CENTER,
            LV_GRAD_CENTER,
            LV_GRAD_RIGHT,
            LV_GRAD_BOTTOM,
            LV_GRAD_EXTEND_PAD
        );

        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        lv_style_set_bg_grad(&style, &grad);

        initialized = true;
    }

    lv_obj_set_style_bg_image_src(
        objects.main, NULL, LV_PART_MAIN
    );

    lv_obj_add_style(
        objects.main, &style, LV_PART_MAIN
    );
}

static void set_settings_button_color(lv_event_t *e, bool pressed)
{
    lv_obj_t *button = lv_event_get_target(e);

    /* Child 0 = Label */
    lv_obj_t *label = lv_obj_get_child(button, 0);

    /* Child 1 = Image */
    lv_obj_t *image = lv_obj_get_child(button, 1);

    if (label == NULL || image == NULL) {
        return;
    }

    lv_obj_set_style_text_color(
        label,
        pressed ? lv_color_hex(0x6e6e6e) : lv_color_hex(0xffffff),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor(
        image,
        lv_color_black(),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor_opa(
        image,
        pressed ? LV_OPA_50 : LV_OPA_TRANSP,
        LV_PART_MAIN
    );
}

/*
 * Drag state:
 * อนุญาตให้ลากได้ครั้งละหนึ่ง Button เท่านั้น
 */
static struct {
    lv_obj_t *button;
    lv_indev_t *indev;
    lv_point_t start_point;
    lv_coord_t start_x;
    lv_coord_t start_y;
    bool active;
} button_drag;

void action_settings_long_pressed(lv_event_t *e)
{
    if (button_drag.active) {
        return;
    }

    lv_obj_t *button = lv_event_get_target(e);
    lv_obj_t *image = lv_obj_get_child(button, 1);

    if (image == NULL) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();

    if (indev == NULL) {
        return;
    }

    button_drag.button = button;
    button_drag.indev = indev;
    button_drag.start_x = lv_obj_get_x(button);
    button_drag.start_y = lv_obj_get_y(button);

    lv_indev_get_point(indev, &button_drag.start_point);

    button_drag.active = true;

    lv_obj_move_foreground(button);

    lv_obj_set_style_transform_pivot_x(
        image, lv_obj_get_width(image) / 2, LV_PART_MAIN
    );

    lv_obj_set_style_transform_pivot_y(
        image, lv_obj_get_height(image) / 2, LV_PART_MAIN
    );

    lv_obj_set_style_transform_scale_x(
        image, 282, LV_PART_MAIN
    );

    lv_obj_set_style_transform_scale_y(
        image, 282, LV_PART_MAIN
    );
}

void action_settings_pressing(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_target(e);

    if (!button_drag.active ||
        button_drag.button != button) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();

    if (indev == NULL || indev != button_drag.indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_obj_set_pos(
        button,
        button_drag.start_x + point.x - button_drag.start_point.x,
        button_drag.start_y + point.y - button_drag.start_point.y
    );
}

void action_settings_pressed(lv_event_t *e)
{
    set_settings_button_color(e, true);
}

void action_settings_released(lv_event_t *e)
{
    set_settings_button_color(e, false);

    lv_obj_t *button = lv_event_get_target(e);

    if (!button_drag.active ||
        button_drag.button != button) {
        return;
    }

    lv_obj_t *image = lv_obj_get_child(button, 1);

    if (image != NULL) {
        lv_obj_set_style_transform_scale_x(
            image, 256, LV_PART_MAIN
        );

        lv_obj_set_style_transform_scale_y(
            image, 256, LV_PART_MAIN
        );
    }

    lv_obj_set_pos(
        button,
        button_drag.start_x,
        button_drag.start_y
    );

    button_drag.active = false;
    button_drag.button = NULL;
    button_drag.indev = NULL;
}

/*
 * Control Center
 * Direct drag + constant-speed linear snap.
 * Background opacity follows overlay position.
 */

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
} control_center_drag;

/*
 * เก็บค่า Max Opacity จาก EEZ Studio
 * อ่านจาก Style ของ Overlay ก่อนเริ่มปรับค่า
 */
static lv_opa_t control_center_max_opa = LV_OPA_COVER;
static bool control_center_opa_initialized = false;

static void control_center_init_opacity(void)
{
    if (control_center_opa_initialized ||
        objects.control_center_overlay == NULL) {
        return;
    }

    control_center_max_opa = lv_obj_get_style_bg_opa(
        objects.control_center_overlay,
        LV_PART_MAIN
    );

    control_center_opa_initialized = true;
}

/*
 * ปรับ Opacity ตามตำแหน่ง Overlay
 *
 * y = -600 : opacity 0
 * y = 0    : opacity เท่ากับ Max จาก EEZ
 */
static void control_center_update_opacity(lv_coord_t y)
{
    lv_obj_t *overlay = objects.control_center_overlay;

    if (overlay == NULL) {
        return;
    }

    control_center_init_opacity();

    int32_t progress = y - CONTROL_CENTER_HIDDEN_Y;

    if (progress < 0) {
        progress = 0;
    }
    else if (progress >
             (CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y)) {
        progress =
            CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y;
    }

    int32_t opacity =
        ((int32_t)control_center_max_opa * progress) /
        (CONTROL_CENTER_OPEN_Y - CONTROL_CENTER_HIDDEN_Y);

    lv_obj_set_style_bg_opa(
        overlay,
        (lv_opa_t)opacity,
        LV_PART_MAIN
    );
}

static bool control_center_is_event_target(lv_obj_t *target)
{
    return target != NULL &&
           (target == objects.control_center_overlay ||
            target == objects.control_center_touch_area);
}

static void control_center_anim_exec(void *obj, int32_t y)
{
    lv_obj_t *overlay = (lv_obj_t *)obj;

    lv_obj_set_y(overlay, (lv_coord_t)y);
    control_center_update_opacity((lv_coord_t)y);
}

/*
 * ใช้ความเร็วคงที่ทั้งเปิดและปิด
 * Linear animation ไม่มีการชะลอช่วงท้าย
 */
static void control_center_animate(lv_coord_t target_y)
{
    lv_obj_t *overlay = objects.control_center_overlay;

    if (overlay == NULL) {
        return;
    }

    lv_coord_t current_y = lv_obj_get_y(overlay);
    int32_t distance = target_y - current_y;

    lv_anim_delete(overlay, control_center_anim_exec);

    if (distance == 0) {
        control_center_update_opacity(target_y);
        return;
    }

    int32_t abs_distance =
        (distance >= 0) ? distance : -distance;

    int32_t duration =
        (abs_distance * 1000) / CONTROL_CENTER_ANIM_SPEED;

    if (duration < 1) {
        duration = 1;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlay);
    lv_anim_set_exec_cb(&anim, control_center_anim_exec);
    lv_anim_set_values(&anim, current_y, target_y);
    lv_anim_set_duration(&anim, (uint32_t)duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

/*
 * คำนวณความเร็วแนวตั้ง
 * บวก = ปัดลง, ลบ = ปัดขึ้น
 */
static void control_center_update_velocity(lv_point_t point, uint32_t now)
{
    uint32_t elapsed = now - control_center_drag.last_tick;

    if (elapsed > 0) {
        int32_t delta_y = point.y - control_center_drag.last_point.y;
        int32_t new_velocity =
            (delta_y * 1000) / (int32_t)elapsed;

        control_center_drag.velocity_y =
            (control_center_drag.velocity_y * 2 + new_velocity) / 3;
    }

    control_center_drag.last_point = point;
    control_center_drag.last_tick = now;
}

void action_control_center_pressed(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);

    if (objects.control_center_overlay == NULL ||
        !control_center_is_event_target(target) ||
        control_center_drag.active) {
        return;
    }

    control_center_init_opacity();

    lv_indev_t *indev = lv_indev_active();

    if (indev == NULL) {
        return;
    }

    lv_anim_delete(
        objects.control_center_overlay,
        control_center_anim_exec
    );

    control_center_drag.target = target;
    control_center_drag.indev = indev;
    control_center_drag.start_y =
        lv_obj_get_y(objects.control_center_overlay);

    lv_indev_get_point(indev, &control_center_drag.start_point);

    control_center_drag.last_point =
        control_center_drag.start_point;

    control_center_drag.last_tick = lv_tick_get();
    control_center_drag.velocity_y = 0;
    control_center_drag.active = true;

    lv_obj_move_foreground(objects.control_center_overlay);
}

void action_control_center_pressing(lv_event_t *e)
{
    if (!control_center_drag.active ||
        !control_center_is_event_target(lv_event_get_target(e))) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();

    if (indev == NULL || indev != control_center_drag.indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    control_center_update_velocity(point, lv_tick_get());

    lv_coord_t new_y =
        control_center_drag.start_y +
        point.y - control_center_drag.start_point.y;

    if (new_y < CONTROL_CENTER_HIDDEN_Y) {
        new_y = CONTROL_CENTER_HIDDEN_Y;
    }
    else if (new_y > CONTROL_CENTER_OPEN_Y) {
        new_y = CONTROL_CENTER_OPEN_Y;
    }

    lv_obj_t *overlay = objects.control_center_overlay;

    if (overlay != NULL && lv_obj_get_y(overlay) != new_y) {
        lv_obj_set_y(overlay, new_y);
        control_center_update_opacity(new_y);
    }
}

void action_control_center_released(lv_event_t *e)
{
    if (!control_center_drag.active ||
        !control_center_is_event_target(lv_event_get_target(e))) {
        return;
    }

    lv_obj_t *overlay = objects.control_center_overlay;

    if (overlay == NULL) {
        control_center_drag.active = false;
        control_center_drag.target = NULL;
        control_center_drag.indev = NULL;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(control_center_drag.indev, &point);

    control_center_update_velocity(point, lv_tick_get());

    lv_coord_t current_y = lv_obj_get_y(overlay);

    int32_t total_move =
        point.y - control_center_drag.start_point.y;

    int32_t velocity_y = control_center_drag.velocity_y;

    bool fast_down =
        total_move >= CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y >= CONTROL_CENTER_FLICK_SPEED;

    bool fast_up =
        total_move <= -CONTROL_CENTER_FLICK_MIN_MOVE &&
        velocity_y <= -CONTROL_CENTER_FLICK_SPEED;

    lv_coord_t destination;

    if (fast_down) {
        destination = CONTROL_CENTER_OPEN_Y;
    }
    else if (fast_up) {
        destination = CONTROL_CENTER_HIDDEN_Y;
    }
    else {
        destination =
            (current_y > (CONTROL_CENTER_HIDDEN_Y / 2))
            ? CONTROL_CENTER_OPEN_Y
            : CONTROL_CENTER_HIDDEN_Y;
    }

    control_center_drag.active = false;
    control_center_drag.target = NULL;
    control_center_drag.indev = NULL;
    control_center_drag.velocity_y = 0;

    control_center_animate(destination);
}