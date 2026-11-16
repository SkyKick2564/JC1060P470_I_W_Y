#include "actions.h"
#include "screens.h"

#ifdef EEZ_LVGL_SIMULATOR

/*
 * Full Simulator:
 * ใช้วงกลมซ้อนกันเพื่อจำลอง Radial Gradient
 * เนื่องจาก LVGL Simulator ที่มากับ EEZ
 * ปิด Complex Gradients ไว้ในค่าเริ่มต้น
 */
void action_set_radial_background(lv_event_t * e)
{
    (void)e;

    if (objects.main == NULL) {
        return;
    }

    /* This action is also triggered by SCREEN_LOADED; avoid applying twice. */
    static lv_obj_t *applied_screen = NULL;
    if (applied_screen == objects.main) {
        return;
    }
    applied_screen = objects.main;

    lv_obj_set_style_bg_image_src(
        objects.main, NULL, LV_PART_MAIN
    );

    lv_obj_t *bg = lv_obj_create(objects.main);

    lv_obj_remove_style_all(bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, 1024, 600);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* ส่งพื้นหลังไปอยู่หลัง Widget อื่น */
    lv_obj_move_to_index(bg, 0);

    for (int i = 0; i < 48; i++) {

        int radius = 650 - (625 * i / 47);
        int gray = 0x20 + (0x57 * i / 47);
        int diameter = radius * 2;

        lv_obj_t *circle = lv_obj_create(bg);

        lv_obj_remove_style_all(circle);

        lv_obj_set_size(circle, diameter, diameter);

        lv_obj_set_pos(
            circle,
            (1024 - diameter) / 2,
            (600 - diameter) / 2
        );

        lv_obj_set_style_radius(
            circle, LV_RADIUS_CIRCLE, 0
        );

        lv_obj_set_style_bg_color(
            circle,
            lv_color_make(gray, gray, gray),
            0
        );

        lv_obj_set_style_bg_opa(
            circle, LV_OPA_COVER, 0
        );

        lv_obj_set_style_border_width(
            circle, 0, 0
        );

        lv_obj_clear_flag(
            circle, LV_OBJ_FLAG_SCROLLABLE
        );
    }
}

#else

/*
 * ESP32-P4:
 * ใช้ Radial Gradient จริงของ LVGL 9.5
 */
void action_set_radial_background(lv_event_t * e)
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

#endif