#ifndef INIT_SCREEN_ACTIONS_H
#define INIT_SCREEN_ACTIONS_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t init_screen_actions_init(void);

void init_screen_update_progress(
    uint8_t progress,
    const char *message
);

#endif