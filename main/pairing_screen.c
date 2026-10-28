#include "pairing_screen.h"
#include "pairing_labels.h"
#include "photopainter_board.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#define WIDTH 800u
#define HEIGHT 480u
#define BLACK 0u
#define WHITE 1u
#define BLUE 4u
static void rect(uint8_t *p,unsigned x,unsigned y,unsigned w,unsigned h,uint8_t ink){
    if(x>=WIDTH||y>=HEIGHT)return;
    if(w>WIDTH-x)w=WIDTH-x;
    if(h>HEIGHT-y)h=HEIGHT-y;
    for(unsigned yy=y;yy<y+h;yy++)memset(p+yy*WIDTH+x,ink,w);
}
static void label(uint8_t *p,const pp_label *text,unsigned x,unsigned y,uint8_t ink){
    for(unsigned yy=0;yy<text->height;yy++)for(unsigned xx=0;xx<text->width;xx++){
        unsigned bit=yy*text->width+xx;
        if((text->bits[bit/8]&(1u<<(7-bit%8)))&&x+xx<WIDTH&&y+yy<HEIGHT)p[(y+yy)*WIDTH+x+xx]=ink;
    }
}
static void centered(uint8_t *p,const pp_label *text,unsigned y,uint8_t ink){label(p,text,(WIDTH-text->width)/2,y,ink);}
/* Compact, fixed 5x7 font for the numeric comparison and bounded ASCII name.
 * Glyph shapes are code-defined; all final output is exact device ink. */
static const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. :_/";
static const uint8_t glyphs[][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
 {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},
 {0,0,0,0,0,0,0},{0,4,4,0,4,4,0},{0,0,0,0,0,0,31},{1,1,2,4,8,16,16}
};
static void ascii(uint8_t *p,const char *text,unsigned x,unsigned y,unsigned scale,uint8_t ink){
    for(size_t i=0;text[i];i++,x+=6*scale){
        int c=toupper((unsigned char)text[i]);const char *g=strchr(alphabet,c);if(!g)continue;
        for(unsigned yy=0;yy<7;yy++)for(unsigned xx=0;xx<5;xx++)if(glyphs[g-alphabet][yy]&(1u<<(4-xx)))rect(p,x+xx*scale,y+yy*scale,scale,scale,ink);
    }
}
static void name(uint8_t *p,const char *text,unsigned y){size_t n=strnlen(text,33);if(n>32)n=32;unsigned width=(unsigned)n*18;ascii(p,text,(WIDTH-width)/2,y,3,BLACK);}
bool pp_pair_screen(const pp_pair_view *v,uint8_t *p,size_t n){
    if(!v||!p||n!=WIDTH*HEIGHT)return false;
    memset(p,WHITE,n);
    ascii(p,"PHOTOPAINTER",40,26,2,BLACK);rect(p,40,57,720,2,BLACK);
    if(v->kind==PP_PAIR_VIEW_PROMPT){
        centered(p,&label_pair_title,83,BLACK);name(p,v->name,143);centered(p,&label_compare,185,BLACK);
        char code[8];snprintf(code,sizeof(code),"%06u",(unsigned)(v->code%1000000u));
        for(unsigned i=0;i<6;i++){
            unsigned x=190+i*70+(i>=3?10:0);char digit[2]={code[i],0};
            rect(p,x,231,58,82,BLUE);rect(p,x+2,233,54,78,WHITE);ascii(p,digit,x+9,245,8,BLUE);
        }
        centered(p,&label_confirm,347,BLACK);
    }else{
        const pp_label *title=&label_ready_title,*detail=&label_ready_detail;
        if(v->kind==PP_PAIR_VIEW_BOUND){title=&label_bound_title;detail=&label_bound_detail;}
        if(v->kind==PP_PAIR_VIEW_CANCELLED){title=&label_cancel_title;detail=&label_retry;}
        if(v->kind==PP_PAIR_VIEW_EXPIRED){title=&label_expired_title;detail=&label_retry;}
        if(v->kind==PP_PAIR_VIEW_ERROR){title=&label_error_title;detail=&label_error_detail;}
        centered(p,title,139,BLACK);
        if(v->kind==PP_PAIR_VIEW_BOUND)name(p,v->name,212);
        centered(p,detail,278,BLACK);
    }
    rect(p,40,413,720,2,BLACK);centered(p,&label_reset,435,BLACK);return true;
}
int app_pairing_draw(const pp_pair_view *v){
    uint8_t *pixels=malloc(WIDTH*HEIGHT);if(!pixels)return -5;
    int result=pp_pair_screen(v,pixels,WIDTH*HEIGHT)?photopainter_board_display(pixels,WIDTH*HEIGHT):-1;
    free(pixels);return result;
}
