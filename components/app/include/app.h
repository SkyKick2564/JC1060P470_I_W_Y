#ifndef APP_H
#define APP_H

#include "esp_err.h"

/* Application Handle */
typedef struct app_context *app_handle_t;

esp_err_t app_init(void);

#endif