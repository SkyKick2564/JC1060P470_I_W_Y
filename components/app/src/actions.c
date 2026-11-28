#include "actions.h"

#include "init_screen_actions.h"
#include "home_actions.h"
#include "control_center_actions.h"

esp_err_t app_actions_init(void)
{
    esp_err_t err = init_screen_actions_init();
    if (err != ESP_OK) {
        return err;
    }

    err = home_actions_init();
    if (err != ESP_OK) {
        return err;
    }

    return control_center_actions_init();
}
