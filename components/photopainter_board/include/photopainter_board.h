#pragma once
#include <stdint.h>
#include <stddef.h>
int photopainter_board_display(const uint8_t *pixels,size_t bytes);
int photopainter_board_pack(const uint8_t *pixels,size_t bytes,uint8_t *wire,size_t wire_bytes);
