/*
 * SPDX-FileCopyrightText: 2026 M1nt-Ch0c0
 * SPDX-License-Identifier: MIT
 */

#ifndef PHOTOFRAME_H
#define PHOTOFRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define PHOTOFRAME_WIDTH 800u
#define PHOTOFRAME_HEIGHT 480u
#define PHOTOFRAME_MAX_PNG_BYTES (5u * 1024u * 1024u)

typedef enum {
    PHOTOFRAME_OK = 0,
    PHOTOFRAME_ERR_ARGUMENT = -1,
    PHOTOFRAME_ERR_PNG = -2,
    PHOTOFRAME_ERR_GEOMETRY = -3,
    PHOTOFRAME_ERR_COLOR = -4,
    PHOTOFRAME_ERR_ALLOCATION = -5,
    PHOTOFRAME_ERR_IO = -6,
    PHOTOFRAME_ERR_BUSY_TIMEOUT = -7,
} photoframe_result_t;

/*
 * Decode, strictly validate, rotate, and synchronously display one PNG.
 * Returns PHOTOFRAME_OK only after the final panel POWER_OFF BUSY wait.
 */
__attribute__((visibility("default")))
int photoframe_render_png(const uint8_t *png_data, size_t png_size);

#ifdef __cplusplus
}
#endif

#endif
