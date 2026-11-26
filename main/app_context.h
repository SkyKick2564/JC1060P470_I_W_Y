#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "board_battery.h"
#include "network_manager.h"
#include "ota_manager.h"

typedef struct {
    network_manager_handle_t network;
    board_battery_handle_t battery;
    ota_manager_handle_t ota;
} app_context_t;

#endif /* APP_CONTEXT_H */