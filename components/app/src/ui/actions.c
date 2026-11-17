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
    /* ถ้ามี Button อื่นกำลังถูกลากอยู่ ไม่อนุญาตให้เริ่ม Drag */
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

    /* เก็บข้อมูล Button และ Input Device ที่เริ่มลาก */
    button_drag.button = button;
    button_drag.indev = indev;
    button_drag.start_x = lv_obj_get_x(button);
    button_drag.start_y = lv_obj_get_y(button);

    lv_indev_get_point(indev, &button_drag.start_point);

    button_drag.active = true;

    /* นำ Button ขึ้นมาอยู่ด้านหน้าสุด */
    lv_obj_move_foreground(button);

    /* ขยาย Image 110% จากจุดกึ่งกลาง */
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

    /* อนุญาตให้เคลื่อนที่เฉพาะ Button ที่เป็นเจ้าของ Drag */
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

    /* คืนสถานะ Drag เฉพาะ Button เจ้าของ Drag */
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

    /* คืนตำแหน่ง Button */
    lv_obj_set_pos(
        button,
        button_drag.start_x,
        button_drag.start_y
    );

    /* ปลด Drag Lock */
    button_drag.active = false;
    button_drag.button = NULL;
    button_drag.indev = NULL;
}