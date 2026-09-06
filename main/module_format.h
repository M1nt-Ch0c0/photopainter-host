#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MODULE_MAGIC 0x31464c45u
#define MODULE_ABI 1u
#define MODULE_HEADER_BYTES 4096u
#define MODULE_SLOT_BYTES (1024u * 1024u)
#define MODULE_MAX_BYTES (MODULE_SLOT_BYTES - MODULE_HEADER_BYTES)
typedef struct
{
    uint32_t magic, format, abi, length, version;
    uint8_t sha256[32];
} module_header_t;
_Static_assert(sizeof(module_header_t) == 52, "package header layout");
bool module_elf_valid(const uint8_t *data, size_t length);
