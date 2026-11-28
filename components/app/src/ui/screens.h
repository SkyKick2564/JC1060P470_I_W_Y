#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_INIT_SCREEN = 1,
    SCREEN_ID_SCREEN_CRITICAL_ERROR = 2,
    SCREEN_ID_MAIN = 3,
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *init_screen;
    lv_obj_t *screen_critical_error;
    lv_obj_t *main;
    lv_obj_t *logo;
    lv_obj_t *bar_loading;
    lv_obj_t *loading_txt;
    lv_obj_t *ctn_head;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *ctn_content;
    lv_obj_t *img_icon;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *txt_component;
    lv_obj_t *txt_error_description;
    lv_obj_t *txt_error_code;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *txt_action_required;
    lv_obj_t *obj14;
    lv_obj_t *top_container;
    lv_obj_t *battery_container;
    lv_obj_t *battery_outline;
    lv_obj_t *battery_level;
    lv_obj_t *battery_terminal;
    lv_obj_t *per_batt_txt;
    lv_obj_t *wifi_container;
    lv_obj_t *wifi_arc_1;
    lv_obj_t *wifi_arc_2;
    lv_obj_t *wifi_arc_3;
    lv_obj_t *txt_clock;
    lv_obj_t *content_container;
    lv_obj_t *home;
    lv_obj_t *wg_network;
    lv_obj_t *camera_btn;
    lv_obj_t *media_btn;
    lv_obj_t *setting_btn;
    lv_obj_t *spotify_btn;
    lv_obj_t *wg_media;
    lv_obj_t *video_image;
    lv_obj_t *setting;
    lv_obj_t *header_container;
    lv_obj_t *label_title;
    lv_obj_t *back_setting_btn;
    lv_obj_t *arrow_txt;
    lv_obj_t *obj15;
    lv_obj_t *settings_content;
    lv_obj_t *obj16;
    lv_obj_t *control_center_overlay;
    lv_obj_t *control_center_touch_area;
} objects_t;

extern objects_t objects;

void create_screen_init_screen();
void delete_screen_init_screen();
void tick_screen_init_screen();

void create_screen_screen_critical_error();
void delete_screen_screen_critical_error();
void tick_screen_screen_critical_error();

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/