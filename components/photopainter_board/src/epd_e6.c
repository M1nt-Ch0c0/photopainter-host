/*
 * SPDX-FileCopyrightText: 2026 M1nt-Ch0c0
 * SPDX-FileCopyrightText: 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 * SPDX-FileCopyrightText: 2025 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * The E6 command/data sequence is adapted from Waveshare's PhotoPainter
 * display_bsp.cpp at commit a5e8f757ba0cafbb5586f07d3e83bda3184c0845.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "photoframe.h"
#include "photoframe_internal.h"

#define EPD_PIN_SCLK GPIO_NUM_10
#define EPD_PIN_MOSI GPIO_NUM_11
#define EPD_PIN_DC GPIO_NUM_8
#define EPD_PIN_CS GPIO_NUM_9
#define EPD_PIN_RST GPIO_NUM_12
#define EPD_PIN_BUSY GPIO_NUM_13

#define EPD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define EPD_TRANSFER_CHUNK 4096u
#define EPD_BUSY_TIMEOUT_MS 120000u
#define EPD_BUSY_POLL_MS 10u

typedef struct {
    spi_device_handle_t spi;
    bool bus_initialized;
    bool gpio_started;
    uint8_t *dma_buffer;
} epd_context_t;

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[6];
} epd_setting_t;

static const epd_setting_t EPD_INIT_SETTINGS[] = {
    {0xaa, 6, {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}},
    {0x01, 1, {0x3f}},
    {0x00, 2, {0x5f, 0x69}},
    {0x03, 4, {0x00, 0x54, 0x00, 0x44}},
    {0x05, 4, {0x40, 0x1f, 0x1f, 0x2c}},
    {0x06, 4, {0x6f, 0x1f, 0x17, 0x49}},
    {0x08, 4, {0x6f, 0x1f, 0x1f, 0x22}},
    {0x30, 1, {0x03}},
    {0x50, 1, {0x3f}},
    {0x60, 2, {0x02, 0x00}},
    {0x61, 4, {0x03, 0x20, 0x01, 0xe0}},
    {0x84, 1, {0x01}},
    {0xe3, 1, {0x2f}},
};

static int set_level(gpio_num_t pin, uint32_t level)
{
    return gpio_set_level(pin, level) == ESP_OK ? PHOTOFRAME_OK
                                                : PHOTOFRAME_ERR_IO;
}

static int spi_write_byte(epd_context_t *context, uint8_t value)
{
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
    };
    transaction.tx_data[0] = value;
    return spi_device_polling_transmit(context->spi, &transaction) == ESP_OK
               ? PHOTOFRAME_OK
               : PHOTOFRAME_ERR_IO;
}

static int epd_write_byte(epd_context_t *context, uint32_t dc_level,
                          uint8_t value)
{
    int result = set_level(EPD_PIN_DC, dc_level);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    result = set_level(EPD_PIN_CS, 0);
    if (result != PHOTOFRAME_OK) {
        return result;
    }

    result = spi_write_byte(context, value);
    int cs_result = set_level(EPD_PIN_CS, 1);
    return result != PHOTOFRAME_OK ? result : cs_result;
}

static int epd_command(epd_context_t *context, uint8_t command)
{
    return epd_write_byte(context, 0, command);
}

static int epd_data(epd_context_t *context, uint8_t data)
{
    return epd_write_byte(context, 1, data);
}

static int epd_setting(epd_context_t *context, uint8_t command,
                       const uint8_t *data, size_t length)
{
    int result = epd_command(context, command);
    for (size_t index = 0; result == PHOTOFRAME_OK && index < length; ++index) {
        result = epd_data(context, data[index]);
    }
    return result;
}

static int epd_wait_ready(const char *stage)
{
    fprintf(stderr, "photoframe: wait %s BUSY=%d\n", stage,
            gpio_get_level(EPD_PIN_BUSY));
    const TickType_t timeout = pdMS_TO_TICKS(EPD_BUSY_TIMEOUT_MS);
    const TickType_t start = xTaskGetTickCount();

    while (gpio_get_level(EPD_PIN_BUSY) == 0) {
        if ((TickType_t)(xTaskGetTickCount() - start) >= timeout) {
            fprintf(stderr, "photoframe: BUSY timeout at %s\n", stage);
            return PHOTOFRAME_ERR_BUSY_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(EPD_BUSY_POLL_MS));
    }
    fprintf(
        stderr, "photoframe: ready %s after %lu ms\n", stage,
        (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));

    return PHOTOFRAME_OK;
}

static int epd_reset(void)
{
    int result = set_level(EPD_PIN_RST, 1);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    result = set_level(EPD_PIN_RST, 0);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    result = set_level(EPD_PIN_RST, 1);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return PHOTOFRAME_OK;
}

static int epd_open(epd_context_t *context)
{
    /* Allocate every module-owned transfer buffer before changing any pin. */
    context->dma_buffer = heap_caps_malloc(
        EPD_TRANSFER_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (context->dma_buffer == NULL) {
        return PHOTOFRAME_ERR_ALLOCATION;
    }

    /* EPD_VCC is supplied by AXP2101 ALDO4.  Validate the PMIC identity,
     * program 3.3 V, enable the rail, and verify both writes before touching
     * any EPD GPIO or initializing its SPI bus. */
    int result = photoframe_axp2101_enable_epd();
    if (result != PHOTOFRAME_OK) {
        fprintf(stderr, "photoframe: power setup failed: %d\n", result);
        return result;
    }

    /* Latch inactive levels before enabling the GPIO outputs. */
    context->gpio_started = true;
    if (set_level(EPD_PIN_CS, 1) != PHOTOFRAME_OK ||
        set_level(EPD_PIN_RST, 1) != PHOTOFRAME_OK ||
        set_level(EPD_PIN_DC, 0) != PHOTOFRAME_OK) {
        return PHOTOFRAME_ERR_IO;
    }

    const gpio_config_t output_config = {
        .pin_bit_mask =
            (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_CS) | (1ULL << EPD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&output_config) != ESP_OK) {
        return PHOTOFRAME_ERR_IO;
    }

    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&input_config) != ESP_OK) {
        return PHOTOFRAME_ERR_IO;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = EPD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = EPD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = EPD_TRANSFER_CHUNK,
    };
    if (spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        return PHOTOFRAME_ERR_IO;
    }
    context->bus_initialized = true;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = EPD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = GPIO_NUM_NC,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    if (spi_bus_add_device(SPI3_HOST, &device_config, &context->spi) !=
        ESP_OK) {
        return PHOTOFRAME_ERR_IO;
    }
    return PHOTOFRAME_OK;
}

static int epd_close(epd_context_t *context)
{
    int result = PHOTOFRAME_OK;
    if (context->gpio_started) {
        (void)set_level(EPD_PIN_CS, 1);
    }

    if (context->spi != NULL) {
        if (spi_bus_remove_device(context->spi) != ESP_OK) {
            result = PHOTOFRAME_ERR_IO;
        }
        context->spi = NULL;
    }
    if (context->bus_initialized) {
        if (spi_bus_free(SPI3_HOST) != ESP_OK) {
            result = PHOTOFRAME_ERR_IO;
        }
        context->bus_initialized = false;
    }
    heap_caps_free(context->dma_buffer);
    context->dma_buffer = NULL;
    return result;
}

static int epd_initialize(epd_context_t *context)
{
    int result = epd_reset();
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    fprintf(stderr, "photoframe: reset BUSY=%d\n",
            gpio_get_level(EPD_PIN_BUSY));
    result = epd_wait_ready("reset");
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    for (size_t index = 0;
         index < sizeof(EPD_INIT_SETTINGS) / sizeof(EPD_INIT_SETTINGS[0]);
         ++index) {
        const epd_setting_t *setting = &EPD_INIT_SETTINGS[index];
        result = epd_setting(context, setting->command, setting->data,
                             setting->length);
        if (result != PHOTOFRAME_OK) {
            return result;
        }
    }

    result = epd_command(context, 0x04);
    return result == PHOTOFRAME_OK ? epd_wait_ready("initial power-on")
                                   : result;
}

static int epd_write_frame(epd_context_t *context, const uint8_t *wire_data,
                           size_t wire_size)
{
    int result = epd_command(context, 0x10);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    result = set_level(EPD_PIN_DC, 1);
    if (result != PHOTOFRAME_OK) {
        return result;
    }
    result = set_level(EPD_PIN_CS, 0);
    if (result != PHOTOFRAME_OK) {
        return result;
    }

    size_t offset = 0;
    while (result == PHOTOFRAME_OK && offset < wire_size) {
        size_t length = wire_size - offset;
        if (length > EPD_TRANSFER_CHUNK) {
            length = EPD_TRANSFER_CHUNK;
        }
        memcpy(context->dma_buffer, wire_data + offset, length);
        spi_transaction_t transaction = {
            .length = length * 8u,
            .tx_buffer = context->dma_buffer,
        };
        if (spi_device_polling_transmit(context->spi, &transaction) != ESP_OK) {
            result = PHOTOFRAME_ERR_IO;
        }
        offset += length;
    }

    int cs_result = set_level(EPD_PIN_CS, 1);
    return result != PHOTOFRAME_OK ? result : cs_result;
}

static int epd_refresh_and_power_off(epd_context_t *context)
{
    static const uint8_t second_setting[] = {0x6f, 0x1f, 0x17, 0x49};
    static const uint8_t zero[] = {0x00};

    int result = epd_command(context, 0x04);
    if (result == PHOTOFRAME_OK)
        result = epd_wait_ready("second power-on");
    if (result == PHOTOFRAME_OK) {
        result =
            epd_setting(context, 0x06, second_setting, sizeof(second_setting));
    }
    if (result == PHOTOFRAME_OK) {
        result = epd_setting(context, 0x12, zero, sizeof(zero));
    }
    if (result == PHOTOFRAME_OK) {
        result = epd_wait_ready("refresh");
    }
    if (result == PHOTOFRAME_OK) {
        result = epd_setting(context, 0x02, zero, sizeof(zero));
    }
    if (result == PHOTOFRAME_OK) {
        /* Success is impossible until this final POWER_OFF wait completes. */
        result = epd_wait_ready("power-off");
    }
    return result;
}

int photoframe_e6_render(const uint8_t *wire_data, size_t wire_size)
{
    if (wire_data == NULL || wire_size != PHOTOFRAME_FRAME_BYTES) {
        return PHOTOFRAME_ERR_ARGUMENT;
    }

    epd_context_t context = {0};
    fprintf(stderr, "photoframe: opening display\n");
    int result = epd_open(&context);
    fprintf(stderr, "photoframe: open result=%d\n", result);
    if (result == PHOTOFRAME_OK) {
        fprintf(stderr, "photoframe: initializing E6\n");
        result = epd_initialize(&context);
        fprintf(stderr, "photoframe: initialize result=%d\n", result);
    }
    if (result == PHOTOFRAME_OK) {
        result = epd_write_frame(&context, wire_data, wire_size);
        fprintf(stderr, "photoframe: frame transfer result=%d\n", result);
    }
    if (result == PHOTOFRAME_OK) {
        result = epd_refresh_and_power_off(&context);
        fprintf(stderr, "photoframe: refresh and final power-off result=%d\n",
                result);
    }

    int close_result = epd_close(&context);
    return result != PHOTOFRAME_OK ? result : close_result;
}
