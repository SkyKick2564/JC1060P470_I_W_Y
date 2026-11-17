#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: Card_Dark
//

void init_style_card_dark_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0x353535));
    lv_style_set_bg_opa(style, 100);
    lv_style_set_bg_grad_color(style, lv_color_hex(0x191919));
    lv_style_set_border_color(style, lv_color_hex(0x414141));
    lv_style_set_border_opa(style, 255);
    lv_style_set_border_width(style, 1);
    lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);
    lv_style_set_radius(style, 20);
};

lv_style_t *get_style_card_dark_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_card_dark_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_card_dark(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_card_dark_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_card_dark(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_card_dark_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Text_Title
//

void init_style_text_title_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_noto_sans_thai_22);
    lv_style_set_text_color(style, lv_color_hex(0xffffff));
};

lv_style_t *get_style_text_title_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_text_title_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_text_title(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_text_title_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_text_title(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_text_title_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Text_Value
//

void init_style_text_value_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0x22d705));
    lv_style_set_text_font(style, &ui_font_noto_sans_thai_32);
};

lv_style_t *get_style_text_value_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_text_value_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_text_value(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_text_value_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_text_value(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_text_value_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Text_Subtitle
//

void add_style_text_subtitle(lv_obj_t *obj) {
    (void)obj;
};

void remove_style_text_subtitle(lv_obj_t *obj) {
    (void)obj;
};

//
// Style: levelStyle
//

void add_style_level_style(lv_obj_t *obj) {
    (void)obj;
};

void remove_style_level_style(lv_obj_t *obj) {
    (void)obj;
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_card_dark,
        add_style_text_title,
        add_style_text_value,
        add_style_text_subtitle,
        add_style_level_style,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_card_dark,
        remove_style_text_title,
        remove_style_text_value,
        remove_style_text_subtitle,
        remove_style_level_style,
    };
    remove_style_funcs[styleIndex](obj);
}