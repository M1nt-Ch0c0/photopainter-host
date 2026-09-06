#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { int e_ = (expr); if (e_) return e_; } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) ((void)(expr))
