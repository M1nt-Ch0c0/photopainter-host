#include "app_manifest.h"
#include "module_format.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
static bool span(size_t o,size_t n,size_t size){return o<=size && n<=size-o;}
static bool printable(const char *s,size_t n,bool id) {
    size_t len=strnlen(s,n);if(!len || len>=n)return false;
    for(size_t i=0;i<len;i++) {
        unsigned char c=s[i];
        if(id ? !((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-') : (c<32||c>126))return false;
    }
    return true;
}
static bool allowed(const char *s) {
    static const char *const symbols[]={
#define APP_IMPORT(name) name,
#include "../sdk/abi2_imports.inc"
#undef APP_IMPORT
    };
    for(size_t i=0;i<sizeof(symbols)/sizeof(*symbols);i++)if(!strcmp(s,symbols[i]))return true;
    return false;
}
bool app_manifest_read(const uint8_t *p,size_t n,app_manifest_v2_t *out) {
    if(!out || !module_elf_valid(p,n))return false;
    uint32_t off=u32(p+32);uint16_t count=u16(p+48),names=u16(p+50);
    const uint8_t *ns=p+off+names*40;uint32_t nso=u32(ns+16),nss=u32(ns+20);
    bool found=false;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *s=p+off+i*40;uint32_t name=u32(s),type=u32(s+4),start=u32(s+16),size=u32(s+20);
        if(name>=nss || !span(nso,nss,n) || !memchr(p+nso+name,0,nss-name))return false;
        if(!strcmp((const char *)p+nso+name,".app_manifest")) {
            if(found || type!=1 || size!=sizeof(*out) || !span(start,size,n) || (u32(s+8)&1))return false;
            memcpy(out,p+start,sizeof(*out));found=true;
        }
        if(type==11) {
            const uint8_t *st=p+off+u32(s+24)*40;uint32_t so=u32(st+16),ss=u32(st+20);
            for(uint32_t j=0;j<size;j+=16) {
                const uint8_t *sym=p+start+j;
                if(u16(sym+14)!=0)continue;
                uint32_t k=u32(sym);if(k>=ss||!span(so,ss,n)||!memchr(p+so+k,0,ss-k))return false;
                const char *text=(const char *)p+so+k;
                if(text[0] && !allowed(text))return false;
            }
        }
    }
    return found && out->magic==APP_MANIFEST_MAGIC && out->size==sizeof(*out) &&
        out->abi_major==2 && out->abi_minor<=APP_ABI_MINOR && !(out->inputs&~APP_INPUT_PNG) &&
        !(out->flags&~APP_REQUIRES_DISPLAY) && !out->reserved[0] && !out->reserved[1] &&
        printable(out->id,sizeof(out->id),true) && printable(out->name,sizeof(out->name),false);
}
