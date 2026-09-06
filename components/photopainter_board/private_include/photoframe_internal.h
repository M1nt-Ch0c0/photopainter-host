/*
 * SPDX-FileCopyrightText: 2026 M1nt-Ch0c0
 * SPDX-License-Identifier: MIT
 */

#ifndef PHOTOFRAME_INTERNAL_H
#define PHOTOFRAME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define PHOTOFRAME_FRAME_BYTES ((800u * 480u) / 2u)

#define PHOTOFRAME_AXP2101_ALDO4_VOLTAGE_MASK 0x1fu
#define PHOTOFRAME_AXP2101_ALDO4_3300MV_CODE 0x1cu
#define PHOTOFRAME_AXP2101_ALDO4_ENABLE_BIT 0x08u

static inline uint8_t photoframe_axp2101_aldo4_voltage_value(
    uint8_t current)
{
    return (uint8_t)((current &
                      (uint8_t)~PHOTOFRAME_AXP2101_ALDO4_VOLTAGE_MASK) |
                     PHOTOFRAME_AXP2101_ALDO4_3300MV_CODE);
}

static inline uint8_t photoframe_axp2101_aldo4_enable_value(uint8_t current)
{
    return (uint8_t)(current | PHOTOFRAME_AXP2101_ALDO4_ENABLE_BIT);
}

int photoframe_decode_png(const uint8_t *png_data,
                          size_t png_size,
                          uint8_t **wire_data);
int photoframe_axp2101_enable_epd(void);
int photoframe_e6_render(const uint8_t *wire_data, size_t wire_size);

#endif
