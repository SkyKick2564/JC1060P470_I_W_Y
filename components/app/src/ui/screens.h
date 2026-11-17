#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
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
    lv_obj_t *content_container___;
    lv_obj_t *home;
    lv_obj_t *widget_container;
    lv_obj_t *card_network;
    lv_obj_t *app_grid_container;
    lv_obj_t *setting_btn;
    lv_obj_t *camera_btn;
    lv_obj_t *setting;
    lv_obj_t *obj0;
} objects_t;

extern objects_t objects;

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