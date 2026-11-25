#ifndef BOARD_BATTERY_PRIVATE_H
#define BOARD_BATTERY_PRIVATE_H

#include "board_battery.h"

#include <stdbool.h>
#include <stdint.h>

#include "periph_adc.h"

#include "adc_battery_estimation.h"

#include "esp_adc/adc_cali.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define BOARD_BATTERY_TASK_STACK_SIZE    4096
#define BOARD_BATTERY_TASK_STACK_DEPTH   \
    (BOARD_BATTERY_TASK_STACK_SIZE / sizeof(StackType_t))

#define BOARD_BATTERY_TASK_PRIORITY      5
#define BOARD_BATTERY_NVS_NAMESPACE      "board_battery"

typedef struct board_battery_context {
    /* Board Manager ADC */
    periph_adc_handle_t *adc_periph;
    periph_adc_config_t *adc_config;

    /* ADC Calibration */
    adc_cali_handle_t adc_cali_handle;

    /* Battery Estimation */
    adc_battery_estimation_handle_t estimation_handle;

    /* FreeRTOS */
    StaticTask_t task_buffer;
    StackType_t task_stack[BOARD_BATTERY_TASK_STACK_DEPTH];
    StaticSemaphore_t task_exit_sem_buffer;
    SemaphoreHandle_t task_exit_sem;
    TaskHandle_t task;
    
    StaticSemaphore_t mutex_buffer;
    SemaphoreHandle_t mutex;

    /* Latest Battery Data */
    uint32_t voltage_mv;
    uint8_t percentage;
    bool is_charging;

    /* State */
    bool initialized;
    bool stopping;

} board_battery_context_t;

#endif /* BOARD_BATTERY_PRIVATE_H */
