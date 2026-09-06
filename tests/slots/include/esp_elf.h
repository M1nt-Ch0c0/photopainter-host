#pragma once
#include <stddef.h>
typedef struct { int live; } esp_elf_t;
typedef struct { const char *name; void *symbol; } esp_elf_symbol_table_t;
int esp_elf_init(esp_elf_t *elf);
int esp_elf_relocate(esp_elf_t *elf, const unsigned char *data);
void esp_elf_deinit(esp_elf_t *elf);
int esp_elf_register_symbol(esp_elf_symbol_table_t *table);
int esp_elf_request(esp_elf_t *elf, int opt, int argc, char **argv);
