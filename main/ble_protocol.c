#include "ble_protocol.h"
#include "module_format.h"
#include <limits.h>
#include <string.h>

uint32_t pp_ble_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
void pp_ble_put32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static bool equal(const uint8_t *a,const uint8_t *b,size_t n) {
    unsigned d=0;
    for (size_t i=0;i<n;i++) d|=a[i]^b[i];
    return d==0;
}
bool pp_ble_tag(const uint8_t *key,size_t key_size,const char *domain,
                const uint8_t *data,size_t size,uint8_t out[32]) {
    uint8_t message[PP_BLE_FRAME_MAX+32];
    size_t n=strlen(domain);
    if(n>32 || size>PP_BLE_FRAME_MAX) return false;
    memcpy(message,domain,n);memcpy(message+n,data,size);
    return pp_ble_hmac(key,key_size,message,n+size,out);
}
static bool token_valid(const char *p) {
    size_t n=strnlen(p,129);
    if(n<32 || n>128) return false;
    for(size_t i=0;i<n;i++) if((uint8_t)p[i]<0x21 || (uint8_t)p[i]>0x7e) return false;
    return true;
}
static bool app_valid(const uint8_t *p) {
    size_t n=0;
    while(n<32 && p[n]) {
        uint8_t c=p[n++];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return false;
    }
    if(!n || n==32) return false;
    for(;n<32;n++) if(p[n]) return false;
    return true;
}
void pp_ble_images_init(pp_ble_images *images,const uint8_t boot[16]) {
    memset(images,0,sizeof(*images));memcpy(images->boot,boot,16);
}
static bool nonzero_id(const uint8_t *p) {
    unsigned bits=0;for(unsigned i=0;i<16;i++)bits|=p[i];return bits!=0;
}
static pp_ble_image_receipt *receipt(pp_ble_images *images,const uint8_t id[16]) {
    for(unsigned i=0;i<PP_BLE_IMAGE_RECEIPTS;i++)
        if(images->receipts[i].state && equal(images->receipts[i].id,id,16))return &images->receipts[i];
    return NULL;
}
static void abort_upload(pp_ble_session *s) {
    if(s->package) s->ops->free(s->package);
    s->package=NULL;s->total=s->received=0;
    memset(s->app,0,sizeof(s->app));memset(s->digest,0,sizeof(s->digest));
    s->image=false;memset(s->image_id,0,sizeof(s->image_id));
    if(s->held) {s->held=false;s->ops->release();}
}
void pp_ble_close(pp_ble_session *s) {
    abort_upload(s);
    /* Volatile writes keep session keys from surviving teardown. */
    volatile uint8_t *p=(volatile uint8_t *)s;
    for(size_t i=0;i<sizeof(*s);i++) p[i]=0;
}
void pp_ble_open(pp_ble_session *s,const pp_ble_ops *ops,const uint8_t id[6],
                 const uint8_t nonce[32],uint64_t now) {
    memset(s,0,sizeof(*s));s->ops=ops;s->challenge=true;s->started=s->touched=now;
    memcpy(s->id,id,6);memcpy(s->nonce,nonce,32);
}
bool pp_ble_expire(pp_ble_session *s,uint64_t now) {
    /* Closed is a level, not a one-shot event: request handling or a crypto
     * failure may already have cleared the session before transport polls. */
    if(!s->ops) return true;
    if(now-s->touched<PP_BLE_IDLE_MS && now-s->started<PP_BLE_SESSION_MS) return false;
    pp_ble_close(s);return true;
}
static int operation(pp_ble_session *s,uint8_t op,const uint8_t *p,size_t n,
                     const uint8_t info[20],uint8_t *out,size_t *length) {
    *length=0;
    switch(op) {
    case PP_BLE_IMAGE_CAPS: {
        if(n || !s->ops->images || !s->ops->image_ready || !s->ops->render)return PP_BLE_INVALID;
        out[0]=1;memcpy(out+1,s->ops->images->boot,16);
        pp_ble_put32(out+17,APP_IMAGE_MAX_BYTES);out[21]=PP_BLE_IMAGE_RECEIPTS;
        *length=22;return PP_BLE_OK;
    }
    case PP_BLE_IMAGE_RESULT: {
        if(n!=32 || !s->ops->images || !nonzero_id(p+16))return PP_BLE_INVALID;
        if(!equal(p,s->ops->images->boot,16))return PP_BLE_BOOT_CHANGED;
        pp_ble_image_receipt *r=receipt(s->ops->images,p+16);
        memset(out,0,71);*length=71;
        if(r) {
            out[0]=r->state;out[1]=(uint8_t)r->result;out[2]=(uint8_t)(r->result>>8);
            pp_ble_put32(out+3,r->size);memcpy(out+7,r->digest,32);memcpy(out+39,r->app,32);
        }
        return PP_BLE_OK;
    }
    case PP_BLE_IMAGE_BEGIN: {
        if(n!=100 || !s->ops->images || !s->ops->image_ready || !s->ops->render ||
           !nonzero_id(p+16) || !app_valid(p+68))return PP_BLE_INVALID;
        if(!equal(p,s->ops->images->boot,16))return PP_BLE_BOOT_CHANGED;
        uint32_t total=pp_ble_u32(p+32);
        if(total<8 || total>APP_IMAGE_MAX_BYTES)return PP_BLE_INVALID;
        pp_ble_image_receipt *r=receipt(s->ops->images,p+16);
        if(r) {
            if(r->size!=total || !equal(r->digest,p+36,32) || memcmp(r->app,p+68,32))return PP_BLE_STATE;
            pp_ble_put32(out,r->size);out[4]=r->state;
            out[5]=(uint8_t)r->result;out[6]=(uint8_t)(r->result>>8);*length=7;
            return PP_BLE_OK;
        }
        if(s->package) {
            if(!s->image || s->total!=total || !equal(s->image_id,p+16,16) ||
               !equal(s->digest,p+36,32) || memcmp(s->app,p+68,32))return PP_BLE_STATE;
        } else {
            if(!s->ops->acquire())return PP_BLE_BUSY;
            s->held=true;
            int ready=s->ops->image_ready((const char *)p+68);
            if(ready!=PP_BLE_OK){abort_upload(s);return ready;}
            s->package=s->ops->alloc(total);
            if(!s->package){abort_upload(s);return PP_BLE_MEMORY;}
            s->image=true;s->total=total;memcpy(s->image_id,p+16,16);
            memcpy(s->digest,p+36,32);memcpy(s->app,p+68,32);
        }
        pp_ble_put32(out,s->received);out[4]=PP_BLE_IMAGE_UNKNOWN;out[5]=out[6]=0;*length=7;
        return PP_BLE_OK;
    }
    case PP_BLE_INFO:
        if(n) return PP_BLE_INVALID;
        memcpy(out,info,20);*length=20;return PP_BLE_OK;
    case PP_BLE_STATUS: {
        if(n!=4) return PP_BLE_INVALID;
        uint32_t offset=pp_ble_u32(p);
        if(!offset) s->ops->status(s->snapshot,sizeof(s->snapshot));
        size_t total=strnlen(s->snapshot,sizeof(s->snapshot));
        if(!total || total==sizeof(s->snapshot) || offset>=total) return PP_BLE_OFFSET;
        size_t count=total-offset;if(count>400) count=400;
        pp_ble_put32(out,(uint32_t)total);pp_ble_put32(out+4,offset);
        memcpy(out+8,s->snapshot+offset,count);*length=count+8;return PP_BLE_OK;
    }
    case PP_BLE_BEGIN: {
        if(n!=68 || !app_valid(p+36)) return PP_BLE_INVALID;
        uint32_t total=pp_ble_u32(p);
        if(total<MODULE_HEADER_BYTES+52 || total>MODULE_SLOT_BYTES) return PP_BLE_INVALID;
        if(s->package) {
            if(s->image || s->total!=total || !equal(s->digest,p+4,32) || memcmp(s->app,p+36,32)) return PP_BLE_STATE;
        } else {
            if(!s->ops->acquire()) return PP_BLE_BUSY;
            s->held=true;s->package=s->ops->alloc(total);
            if(!s->package) {abort_upload(s);return PP_BLE_MEMORY;}
            s->total=total;memcpy(s->digest,p+4,32);memcpy(s->app,p+36,32);
        }
        pp_ble_put32(out,s->received);*length=4;return PP_BLE_OK;
    }
    case PP_BLE_CHUNK: {
        if(n<5) return PP_BLE_INVALID;
        if(!s->package) return PP_BLE_STATE;
        uint32_t offset=pp_ble_u32(p);size_t count=n-4;
        if(offset>s->received || offset>s->total || count>s->total-offset) return PP_BLE_OFFSET;
        if(offset<s->received) {
            if(count>s->received-offset || memcmp(s->package+offset,p+4,count)) return PP_BLE_OFFSET;
        } else {memcpy(s->package+offset,p+4,count);s->received+=(uint32_t)count;}
        pp_ble_put32(out,s->received);*length=4;return PP_BLE_OK;
    }
    case PP_BLE_COMMIT: {
        if(n) return PP_BLE_INVALID;
        if(!s->package || s->received!=s->total) return PP_BLE_STATE;
        uint8_t digest[32];int result=PP_BLE_HASH;
        if(pp_ble_sha256(s->package,s->total,digest) && equal(digest,s->digest,32)) {
            if(s->image) {
                pp_ble_images *images=s->ops->images;
                pp_ble_image_receipt *r=&images->receipts[images->next++%PP_BLE_IMAGE_RECEIPTS];
                memset(r,0,sizeof(*r));memcpy(r->id,s->image_id,16);memcpy(r->digest,s->digest,32);
                memcpy(r->app,s->app,32);r->size=s->total;r->state=PP_BLE_IMAGE_RUNNING;
                /* Admission and buffer remain owned until the sole manager
                 * returns, even if GAP disconnects while the panel is busy. */
                result=s->ops->render(s->package,s->total);
                r->result=(uint16_t)result;r->state=PP_BLE_IMAGE_COMPLETE;
            } else result=s->ops->stage(s->app,s->package,s->total);
        }
        abort_upload(s);return result;
    }
    case PP_BLE_ABORT:
        if(n) return PP_BLE_INVALID;
        abort_upload(s);return PP_BLE_OK;
    case PP_BLE_PROGRESS:
        if(n) return PP_BLE_INVALID;
        pp_ble_put32(out,s->received);pp_ble_put32(out+4,s->total);*length=8;return PP_BLE_OK;
    case PP_BLE_ACTIVATE: case PP_BLE_SWITCH: case PP_BLE_ROLLBACK: case PP_BLE_REMOVE: {
        if(n!=32 || !app_valid(p)) return PP_BLE_INVALID;
        if(s->held || !s->ops->acquire()) return PP_BLE_BUSY;
        int result=s->ops->manage(op,(const char *)p);
        s->ops->release();return result;
    }
    case PP_BLE_APP_READ: case PP_BLE_APP_WRITE: {
        if(!s->ops->app || n<12 || n>140 || (op==PP_BLE_APP_READ && n!=12))return PP_BLE_INVALID;
        if(s->held || !s->ops->acquire())return PP_BLE_BUSY;
        int result=s->ops->app(op,p,n,out,length);
        s->ops->release();return result;
    }
    default:return PP_BLE_INVALID;
    }
}
size_t pp_ble_request(pp_ble_session *s,const char *token,const uint8_t info[20],
                      const uint8_t *request,size_t size,uint64_t now,uint8_t out[512]) {
    uint8_t op=size?request[0]:0;uint32_t sequence=0;int result=PP_BLE_UNAUTHORIZED;
    size_t length=0;bool sign=false;
    if(!s->ops || pp_ble_expire(s,now)) {result=PP_BLE_EXPIRED;goto reply;}
    if(op==PP_BLE_AUTH) {
        if(!token_valid(token)) {result=PP_BLE_UNCONFIGURED;goto reply;}
        if(!s->challenge || s->authenticated) goto reply;
        s->challenge=false; /* One attempt per fresh connection, including malformed proofs. */
        if(size!=65) goto reply;
        uint8_t transcript[70],proof[32];
        memcpy(transcript,s->id,6);memcpy(transcript+6,s->nonce,32);memcpy(transcript+38,request+1,32);
        if(!pp_ble_tag((const uint8_t *)token,strlen(token),"pp-ble-v1/client",transcript,70,proof) ||
           !equal(proof,request+33,32)) goto reply;
        if(!pp_ble_tag((const uint8_t *)token,strlen(token),"pp-ble-v1/session",transcript,70,s->key)) goto reply;
        s->authenticated=true;s->sequence=0;s->touched=now;sign=true;
        memcpy(out+7,info,20);length=20;result=PP_BLE_OK;
    } else {
        uint8_t tag[32];
        if(size<37 || size>512 || !s->authenticated) goto reply;
        sequence=pp_ble_u32(request+1);
        if(s->sequence==UINT32_MAX || sequence!=s->sequence+1 ||
           !pp_ble_tag(s->key,32,"pp-ble-v1/request",request,size-32,tag) ||
           !equal(tag,request+size-32,32)) goto reply;
        s->sequence=sequence;s->touched=now;sign=true;
        result=operation(s,op,request+5,size-37,info,out+7,&length);
    }
reply:
    out[0]=op;pp_ble_put32(out+1,sequence);out[5]=(uint8_t)result;out[6]=0;
    if(sign) {
        if(!pp_ble_tag(s->key,32,"pp-ble-v1/response",out,length+7,out+length+7)) {
            pp_ble_close(s);out[5]=PP_BLE_UNAUTHORIZED;return 7;
        }
        return length+39;
    }
    return 7; /* Unsigned errors are never evidence of device identity or success. */
}
int pp_ble_fragment(pp_ble_fragments *f,const uint8_t *p,size_t n) {
    if(n<5 || n>512) goto invalid;
    unsigned offset=p[0]|(unsigned)p[1]<<8,total=p[2]|(unsigned)p[3]<<8;
    size_t count=n-4;
    if(!total || total>512 || offset>total || count>total-offset) goto invalid;
    if(!offset && (!f->received || f->total!=total)) {f->received=0;f->total=(uint16_t)total;}
    if(total!=f->total || offset>f->received) goto invalid;
    if(offset<f->received) {
        if(count>f->received-offset || memcmp(f->data+offset,p+4,count)) goto invalid;
    } else {memcpy(f->data+offset,p+4,count);f->received+=(uint16_t)count;}
    return f->received==f->total;
invalid:
    f->received=f->total=0;return -1;
}
