#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: Card_Dark
lv_style_t *get_style_card_dark_MAIN_DEFAULT();
void add_style_card_dark(lv_obj_t *obj);
void remove_style_card_dark(lv_obj_t *obj);

// Style: Text_Title
lv_style_t *get_style_text_title_MAIN_DEFAULT();
void add_style_text_title(lv_obj_t *obj);
void remove_style_text_title(lv_obj_t *obj);

// Style: Text_Value
lv_style_t *get_style_text_value_MAIN_DEFAULT();
void add_style_text_value(lv_obj_t *obj);
void remove_style_text_value(lv_obj_t *obj);

// Style: Text_Subtitle
void add_style_text_subtitle(lv_obj_t *obj);
void remove_style_text_subtitle(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/