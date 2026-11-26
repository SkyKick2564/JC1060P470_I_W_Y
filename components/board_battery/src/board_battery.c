#include "board_battery_private.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_board_manager.h"
#include "periph_adc.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "board_battery";

ESP_EVENT_DEFINE_BASE(BOARD_BATTERY_EVENT);

static void board_battery_task(void *arg);

static esp_err_t battery_voltage_filter(
    board_battery_context_t *ctx,
    int voltage_mv,
    int *filtered_voltage_mv
)
{
    if (ctx == NULL || filtered_voltage_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ctx->voltage_samples[ctx->voltage_sample_index] = voltage_mv;

    ctx->voltage_sample_index = 
        (ctx->voltage_sample_index + 1) % 
        BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES;
    
    if (ctx->voltage_sample_count < BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES) {
        ctx->voltage_sample_count++;
    }

    /* Warm-up: wait until all 9 samples are available */
    if (ctx->voltage_sample_count < BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES) {
        return ESP_ERR_NOT_FINISHED;
    }

    /* Copy samples for sorting */
    int sorted[BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES];
    for (int i = 0; i < BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES; i++) {
        sorted[i] = ctx->voltage_samples[i];
    }
    for (int i = 0; i < BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES - 1; i++) {
        for (int j = i + 1; j < BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES; j++) {
            if (sorted[i] > sorted[j]) {
                int temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    /* Calculate Median */
    int median_mv = sorted[BOARD_BATTERY_VOLTAGE_FILTER_SAMPLES / 2];

    /* Initialize EMA after warm-up */
    if (!ctx->voltage_ema_initialized) {
        ctx->voltage_ema = (float)median_mv;
        ctx->voltage_ema_initialized = true;
    } else {
        /* Apply EMA Low-pass Filter */
        ctx->voltage_ema =
            (BOARD_BATTERY_VOLTAGE_EMA_ALPHA * (float)median_mv) +
            ((1.0f - BOARD_BATTERY_VOLTAGE_EMA_ALPHA) * ctx->voltage_ema);
    }

    *filtered_voltage_mv = (int)(ctx->voltage_ema + 0.5f);

    return ESP_OK;
}

esp_err_t board_battery_init(board_battery_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *handle = NULL;

    board_battery_context_t *ctx = heap_caps_calloc(
        1,
        sizeof(board_battery_context_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err;
    /* Get ADC peripheral handle from Board Manager */
    err = esp_board_manager_get_periph_handle(
        "adc_battery",
        (void **)&ctx->adc_periph
    );
    if (err != ESP_OK || ctx->adc_periph == NULL) {
        ESP_LOGE(TAG, "Failed to Get ADC peripheral: %s",
                 esp_err_to_name(err));
        heap_caps_free(ctx);
        return (err != ESP_OK) ? err : ESP_FAIL;
    }
    ESP_LOGI(TAG, "ADC peripheral handle retrieved: %p",
         (void *)ctx->adc_periph->oneshot);

    /* Get ADC peripheral configuration */
    err = esp_board_manager_get_periph_config(
        "adc_battery",
        (void **)&ctx->adc_config
    );
    if (err != ESP_OK || ctx->adc_config == NULL) {
        ESP_LOGE(TAG, "Failed to Get ADC peripheral config: %s",
                 esp_err_to_name(err));
        heap_caps_free(ctx);
        return (err != ESP_OK) ? err : ESP_FAIL;
    }
    ESP_LOGI(TAG, "ADC configuration retrieved successfully");
    ESP_LOGI(TAG, "  Role       : %d", ctx->adc_config->role);
    ESP_LOGI(TAG, "  Unit       : %d", ctx->adc_config->cfg.oneshot.unit_cfg.unit_id);
    ESP_LOGI(TAG, "  Channel    : %d", ctx->adc_config->cfg.oneshot.channel_id);
    ESP_LOGI(TAG, "  Attenuation: %d", ctx->adc_config->cfg.oneshot.chan_cfg.atten);
    ESP_LOGI(TAG, "  Bit Width  : %d", ctx->adc_config->cfg.oneshot.chan_cfg.bitwidth);

    /* Validate ADC One-shot mode */
    if (ctx->adc_config->role != ESP_BOARD_PERIPH_ROLE_ONESHOT ||
        ctx->adc_periph->oneshot == NULL) {
        ESP_LOGE(TAG, "ADC peripheral is not initialized in one-shot mode");
        heap_caps_free(ctx);
        return ESP_ERR_INVALID_STATE;
    }

    /* Create ADC Calibration */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ctx->adc_config->cfg.oneshot.unit_cfg.unit_id,
        .chan = ctx->adc_config->cfg.oneshot.channel_id,
        .atten = ctx->adc_config->cfg.oneshot.chan_cfg.atten,
        .bitwidth = ctx->adc_config->cfg.oneshot.chan_cfg.bitwidth,
    };

    err = adc_cali_create_scheme_curve_fitting(
        &cali_config,
        &ctx->adc_cali_handle
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to Create ADC Calibration: %s",
                 esp_err_to_name(err));
        heap_caps_free(ctx);
        return err;
    }
    ESP_LOGI(TAG, "ADC Calibration created successfully");

    ctx->mutex = xSemaphoreCreateMutexStatic(&ctx->mutex_buffer);
    if (ctx->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to Create Battery Mutex");
        adc_cali_delete_scheme_curve_fitting(ctx->adc_cali_handle);
        heap_caps_free(ctx);
        return ESP_FAIL;
    }

    ctx->task_exit_sem = xSemaphoreCreateBinaryStatic(
        &ctx->task_exit_sem_buffer
    );
    if (ctx->task_exit_sem == NULL) {
        ESP_LOGE(TAG, "Failed to Create Task Exit Semaphore");
        adc_cali_delete_scheme_curve_fitting(ctx->adc_cali_handle);
        vSemaphoreDelete(ctx->mutex);
        heap_caps_free(ctx);
        return ESP_FAIL;
    }

    ctx->initialized = true;

    ctx->task = xTaskCreateStatic(
        board_battery_task,
        "board_battery_task",
        BOARD_BATTERY_TASK_STACK_DEPTH,
        ctx,
        BOARD_BATTERY_TASK_PRIORITY,
        ctx->task_stack,
        &ctx->task_buffer
    );
    if (ctx->task == NULL) {
        ESP_LOGE(TAG, "Failed to Create Battery Task");
        adc_cali_delete_scheme_curve_fitting(ctx->adc_cali_handle);
        vSemaphoreDelete(ctx->mutex);
        vSemaphoreDelete(ctx->task_exit_sem);
        heap_caps_free(ctx);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Battery Task created successfully");
    *handle = ctx;
    // Initialization logic for board battery
    return ESP_OK;
}

esp_err_t board_battery_deinit(board_battery_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    handle->stopping = true;

    /* Wait for the task to exit */
    if (xSemaphoreTake(handle->task_exit_sem, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    vTaskSuspend(handle->task);
    vTaskDelete(handle->task);
    handle->task = NULL;

    adc_cali_delete_scheme_curve_fitting(handle->adc_cali_handle);
    vSemaphoreDelete(handle->mutex);
    vSemaphoreDelete(handle->task_exit_sem);
    heap_caps_free(handle);

    return ESP_OK;
}

static void board_battery_task(void *arg)
{
    board_battery_context_t *ctx = (board_battery_context_t *)arg;

    while (!ctx->stopping) {
        vTaskDelay(pdMS_TO_TICKS(5000)); /* 1 second delay */
    }

    xSemaphoreGive(ctx->task_exit_sem);
    vTaskSuspend(NULL);
}

