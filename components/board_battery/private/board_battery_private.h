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

#define BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES    9
#define BOARD_BATTERY_VOLTAGE_EMA_ALPHA         0.25f

typedef struct board_battery_context {
    /* Board Manager ADC */
    periph_adc_handle_t *adc_periph;
    periph_adc_config_t *adc_config;

    /* ADC Calibration */
    adc_cali_handle_t adc_cali_handle;

    /* Voltage Filter */
    int voltage_samples[BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES];
    uint8_t voltage_sample_count;
    uint8_t voltage_sample_index;
    float voltage_ema;
    bool voltage_ema_initialized;

    /* FreeRTOS */
    StaticTask_t task_buffer;
    StackType_t task_stack[BOARD_BATTERY_TASK_STACK_DEPTH];
    StaticSemaphore_t task_exit_sem_buffer;
    SemaphoreHandle_t task_exit_sem;
    TaskHandle_t task;
    
    StaticSemaphore_t mutex_buffer;
    SemaphoreHandle_t mutex;

    /* Latest Battery Data */
    board_battery_event_data_t latest_data;

    /* State */
    bool initialized;
    bool stopping;

} board_battery_context_t;

#endif /* BOARD_BATTERY_PRIVATE_H */
