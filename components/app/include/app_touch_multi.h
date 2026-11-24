#ifndef APP_TOUCH_MULTI_H
#define APP_TOUCH_MULTI_H

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#define APP_TOUCH_MULTI_POINTERS 5

esp_err_t app_touch_multi_add(lv_display_t *disp, esp_lcd_touch_handle_t handle);
esp_err_t app_touch_multi_remove(void);

#endif
