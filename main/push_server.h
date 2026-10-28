#pragma once

#include "esp_err.h"

/* v2 exposes liveness and explicit retirement responses, never token control. */
esp_err_t photopainter_push_server_start(void);
