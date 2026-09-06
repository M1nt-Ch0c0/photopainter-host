/* SPDX-License-Identifier: MIT */
#include "photopainter_board.h"
#include "photoframe.h"
#include "photoframe_internal.h"
#include <stdlib.h>
int photopainter_board_pack(const uint8_t *pixels,size_t bytes,uint8_t *wire,size_t wire_bytes) {
    if(!pixels||!wire||bytes!=800u*480u||wire_bytes!=192000u)return PHOTOFRAME_ERR_ARGUMENT;
    /* Validate the entire frame before creating even a partial hardware frame. */
    for(size_t i=0;i<bytes;i++)if(pixels[i]>5)return PHOTOFRAME_ERR_COLOR;
    static const uint8_t codes[]={0,1,2,3,5,6};
    for(size_t i=0;i<bytes;i+=2)wire[wire_bytes-1-i/2]=(codes[pixels[i+1]]<<4)|codes[pixels[i]];
    return PHOTOFRAME_OK;
}
int photopainter_board_display(const uint8_t *pixels,size_t bytes) {
    if(!pixels||bytes!=800u*480u)return PHOTOFRAME_ERR_ARGUMENT;
    uint8_t *wire=malloc(192000u);if(!wire)return PHOTOFRAME_ERR_ALLOCATION;
    int e=photopainter_board_pack(pixels,bytes,wire,192000u);
    if(e==PHOTOFRAME_OK)e=photoframe_e6_render(wire,192000u);
    free(wire);return e;
}
