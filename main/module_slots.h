#pragma once
#include "esp_err.h"
#include "module_format.h"
typedef struct
{
    int32_t active, previous, pending, trial;
} module_state_t;
esp_err_t module_slots_init(void);
module_state_t module_slots_state(void);
esp_err_t module_slots_read(int slot, uint8_t **data, module_header_t *header);
esp_err_t module_slots_stage(const uint8_t *package, size_t size);
esp_err_t module_slots_begin_trial(void);
esp_err_t module_slots_confirm(void);
esp_err_t module_slots_rollback(void);
esp_err_t module_slots_recover(void);
