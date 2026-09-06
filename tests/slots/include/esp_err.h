#pragma once
typedef int esp_err_t;
enum
{
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_INVALID_ARG = 1,
    ESP_ERR_INVALID_STATE = 2,
    ESP_ERR_INVALID_SIZE = 3,
    ESP_ERR_INVALID_VERSION = 4,
    ESP_ERR_INVALID_CRC = 5,
    ESP_ERR_NO_MEM = 6,
    ESP_ERR_NOT_FOUND = 7,
    ESP_ERR_NVS_NOT_FOUND = 8
};
