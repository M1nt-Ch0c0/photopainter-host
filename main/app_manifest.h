#pragma once
#include "../sdk/photopainter_app.h"
#include <stdbool.h>
/* Caller must first pass module_elf_valid(); parser also bounds every read. */
bool app_manifest_read(const uint8_t *elf,size_t size,app_manifest_v2_t *out);
