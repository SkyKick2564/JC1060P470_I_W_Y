/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include "guition_touch.h"
#include "esp_lcd_panel_io_interface.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    esp_lcd_panel_io_t base;
    guition_soft_spi_config_t pins;
    SemaphoreHandle_t lock;
} soft_io_t;

static uint8_t exchange(soft_io_t *io, uint8_t value)
{
    uint8_t input = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(io->pins.mosi, (value >> bit) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(io->pins.sclk, 1);
        input = (input << 1) | gpio_get_level(io->pins.miso);
        esp_rom_delay_us(1);
        gpio_set_level(io->pins.sclk, 0);
    }
    return input;
}

static esp_err_t transfer(esp_lcd_panel_io_t *base, int cmd, const void *tx, void *rx, size_t n)
{
    soft_io_t *io = (soft_io_t *)base;
    if (n && !tx && !rx) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(io->lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    gpio_set_level(io->pins.cs, 0);
    if (cmd >= 0) exchange(io, cmd);
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = exchange(io, tx ? ((const uint8_t *)tx)[i] : 0);
        if (rx) ((uint8_t *)rx)[i] = b;
    }
    gpio_set_level(io->pins.cs, 1);
    xSemaphoreGive(io->lock);
    return ESP_OK;
}
static esp_err_t rx(esp_lcd_panel_io_t *io, int cmd, void *p, size_t n) { return transfer(io, cmd, NULL, p, n); }
static esp_err_t tx(esp_lcd_panel_io_t *io, int cmd, const void *p, size_t n) { return transfer(io, cmd, p, NULL, n); }
static esp_err_t destroy(esp_lcd_panel_io_t *base)
{
    soft_io_t *io = (soft_io_t *)base;
    if (xSemaphoreTake(io->lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    gpio_set_level(io->pins.cs, 1);
    gpio_reset_pin(io->pins.cs);
    gpio_reset_pin(io->pins.sclk);
    gpio_reset_pin(io->pins.mosi);
    gpio_reset_pin(io->pins.miso);
    vSemaphoreDelete(io->lock);
    free(io);
    return ESP_OK;
}
static esp_err_t callbacks(esp_lcd_panel_io_t *io, const esp_lcd_panel_io_callbacks_t *cb, void *ctx)
{ (void)io; (void)cb; (void)ctx; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t guition_soft_spi_new(const guition_soft_spi_config_t *pins, esp_lcd_panel_io_handle_t *out)
{
    if (!pins || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pins->cs) || !GPIO_IS_VALID_OUTPUT_GPIO(pins->sclk) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(pins->mosi) || !GPIO_IS_VALID_GPIO(pins->miso) ||
        pins->cs == pins->sclk || pins->cs == pins->mosi || pins->cs == pins->miso ||
        pins->sclk == pins->mosi || pins->sclk == pins->miso || pins->mosi == pins->miso) return ESP_ERR_INVALID_ARG;
    soft_io_t *io = calloc(1, sizeof(*io));
    if (!io) return ESP_ERR_NO_MEM;
    io->pins = *pins;
    io->lock = xSemaphoreCreateMutex();
    if (!io->lock) { free(io); return ESP_ERR_NO_MEM; }
    io->base = (esp_lcd_panel_io_t){ .rx_param = rx, .tx_param = tx, .tx_color = tx,
        .del = destroy, .register_event_callbacks = callbacks };
    gpio_set_level(pins->cs, 1);
    gpio_config_t cfg = { .pin_bit_mask = (1ULL << pins->cs) | (1ULL << pins->sclk) | (1ULL << pins->mosi), .mode = GPIO_MODE_OUTPUT };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        cfg.pin_bit_mask = 1ULL << pins->miso;
        cfg.mode = GPIO_MODE_INPUT;
        ret = gpio_config(&cfg);
    }
    if (ret != ESP_OK) { destroy(&io->base); return ret; }
    gpio_set_level(pins->cs, 1);
    gpio_set_level(pins->sclk, 0);
    *out = &io->base;
    return ESP_OK;
}
