#include "ble_protocol.h"
#include "module_format.h"
#include <assert.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool pp_ble_sha256(const uint8_t *p,size_t n,uint8_t out[32]) {return SHA256(p,n,out)!=NULL;}
bool pp_ble_hmac(const uint8_t *k,size_t kn,const uint8_t *p,size_t n,uint8_t out[32]) {
    unsigned length=0;return HMAC(EVP_sha256(),k,(int)kn,p,n,out,&length)!=NULL && length==32;
}
static bool busy,oom;
static unsigned stages,managed,allocations,releases;
static int stage_result;
static uint8_t package[5000],info[20]={1,2,0,1,2,3,4,5,192,168,1,42};
static const char token[]="0123456789abcdef0123456789abcdef";
static bool acquire(void) {if(busy) return false;busy=true;return true;}
static void release(void) {assert(busy);busy=false;releases++;}
static void *allocate(size_t n) {if(oom) return NULL;allocations++;return malloc(n);}
static void dispose(void *p) {assert(p && allocations);allocations--;free(p);}
static int stage(const char *id,const uint8_t *p,size_t n) {
    assert(busy && !strcmp(id,"photoframe") && n==sizeof(package));
    assert(!memcmp(p,package,n));stages++;return stage_result;
}
static int manage(uint8_t op,const char *id) {
    assert(busy && op>=PP_BLE_ACTIVATE && op<=PP_BLE_REMOVE && !strcmp(id,"photoframe"));
    managed++;return PP_BLE_OK;
}
static void status(char *p,size_t n) {assert(n>=4096);memset(p,'x',1050);p[1050]=0;}
static const pp_ble_ops ops={acquire,release,allocate,dispose,stage,manage,status};
static pp_ble_session session;
static uint8_t client_key[32],out[512];
static uint32_t seq;
static uint64_t now=10;
static size_t response_size;
static void unhex(const char *p,uint8_t *out) {
    for(size_t i=0;i<32;i++) {unsigned v;assert(sscanf(p+2*i,"%2x",&v)==1);out[i]=(uint8_t)v;}
}
static void open_session(void) {
    pp_ble_close(&session);uint8_t nonce[32];for(unsigned i=0;i<32;i++) nonce[i]=(uint8_t)i;
    pp_ble_open(&session,&ops,info+2,nonce,now);seq=0;
}
static void auth(void) {
    uint8_t request[65]={PP_BLE_AUTH};
    for(unsigned i=0;i<32;i++) request[1+i]=(uint8_t)(32+i);
    /* Fixed independent Python hashlib/HMAC vector, shared with the Go tests. */
    unhex("827d2691b1e2ac5990b86832bbaae8eaa53767b4a807d027d5a13c62cd9b495c",request+33);
    assert(pp_ble_request(&session,token,info,request,sizeof(request),now,out)==59 && out[5]==0);
    unhex("2b1310093d3f4c44d104014ce803b48f19cfefc38a66f583daf21a5a3bd6aa2e",client_key);
    assert(!memcmp(session.key,client_key,32));
    uint8_t tag[32];assert(pp_ble_tag(client_key,32,"pp-ble-v1/response",out,27,tag));
    assert(!memcmp(tag,out+27,32));
}
static int request(uint8_t op,const void *data,size_t n) {
    uint8_t p[512]={op};pp_ble_put32(p+1,++seq);
    if(n) memcpy(p+5,data,n);
    assert(n+37<=sizeof(p));
    assert(pp_ble_tag(client_key,32,"pp-ble-v1/request",p,n+5,p+n+5));
    response_size=pp_ble_request(&session,token,info,p,n+37,now,out);
    assert(response_size>=39 && out[0]==op && pp_ble_u32(out+1)==seq);
    uint8_t tag[32];assert(pp_ble_tag(client_key,32,"pp-ble-v1/response",out,response_size-32,tag));
    assert(!memcmp(tag,out+response_size-32,32));return out[5];
}
static int begin(void) {
    uint8_t p[68]={0};pp_ble_put32(p,sizeof(package));
    pp_ble_sha256(package,sizeof(package),p+4);memcpy(p+36,"photoframe",11);
    return request(PP_BLE_BEGIN,p,sizeof(p));
}
static void fill(void) {
    for(unsigned offset=0;offset<sizeof(package);) {
        uint8_t p[404];size_t count=sizeof(package)-offset;if(count>400) count=400;
        pp_ble_put32(p,offset);memcpy(p+4,package+offset,count);
        assert(request(PP_BLE_CHUNK,p,count+4)==0);offset+=(unsigned)count;
        assert(pp_ble_u32(out+7)==offset);
    }
}
static void test_auth(void) {
    open_session();uint8_t invalid[65]={PP_BLE_AUTH};
    assert(pp_ble_request(&session,token,info,invalid,65,now,out)==7 && out[5]==PP_BLE_UNAUTHORIZED);
    assert(!session.challenge && !session.authenticated);
    open_session();assert(pp_ble_request(&session,"",info,invalid,65,now,out)==7 && out[5]==PP_BLE_UNCONFIGURED);
    open_session();auth();assert(request(PP_BLE_INFO,NULL,0)==0 && !memcmp(out+7,info,20));
    uint8_t replay[37]={PP_BLE_INFO};pp_ble_put32(replay+1,seq);
    pp_ble_tag(client_key,32,"pp-ble-v1/request",replay,5,replay+5);
    assert(pp_ble_request(&session,token,info,replay,sizeof(replay),now,out)==7 && out[5]==PP_BLE_UNAUTHORIZED);
    pp_ble_put32(replay+1,seq+1); /* Valid sequence with modified bytes is rejected. */
    assert(pp_ble_request(&session,token,info,replay,sizeof(replay),now,out)==7);
    assert(request(PP_BLE_INFO,NULL,0)==0); /* Invalid message did not consume sequence. */
    open_session();assert(pp_ble_request(&session,token,info,replay,sizeof(replay),now,out)==7);
    auth();assert(begin()==0 && busy);
    now+=PP_BLE_IDLE_MS;assert(pp_ble_expire(&session,now));assert(!busy && !allocations);
    open_session();auth();session.sequence=UINT32_MAX;seq=UINT32_MAX;
    memset(replay,0,sizeof(replay));replay[0]=PP_BLE_INFO;
    pp_ble_tag(client_key,32,"pp-ble-v1/request",replay,5,replay+5);
    assert(pp_ble_request(&session,token,info,replay,sizeof(replay),now,out)==7);
}
static void test_transfer(void) {
    open_session();auth();busy=true;assert(begin()==PP_BLE_BUSY && !allocations);busy=false;
    oom=true;assert(begin()==PP_BLE_MEMORY && !busy);oom=false;
    assert(begin()==0 && busy);assert(begin()==0 && pp_ble_u32(out+7)==0 && allocations==1);
    assert(request(PP_BLE_COMMIT,NULL,0)==PP_BLE_STATE && !stages);
    uint8_t p[40]={0};pp_ble_put32(p,1);assert(request(PP_BLE_CHUNK,p,sizeof(p))==PP_BLE_OFFSET);
    pp_ble_put32(p,0);memcpy(p+4,package,36);assert(request(PP_BLE_CHUNK,p,sizeof(p))==0);
    assert(request(PP_BLE_CHUNK,p,sizeof(p))==0 && pp_ble_u32(out+7)==36);
    p[4]^=1;assert(request(PP_BLE_CHUNK,p,sizeof(p))==PP_BLE_OFFSET);
    pp_ble_put32(p,UINT32_MAX);assert(request(PP_BLE_CHUNK,p,sizeof(p))==PP_BLE_OFFSET);
    uint8_t id[32]={0};strcpy((char *)id,"photoframe");
    assert(request(PP_BLE_ACTIVATE,id,32)==PP_BLE_BUSY && !managed);
    assert(request(PP_BLE_ABORT,NULL,0)==0 && !busy);
    assert(begin()==0);fill();session.package[100]^=1;
    assert(request(PP_BLE_COMMIT,NULL,0)==PP_BLE_HASH && !stages && !busy);
    assert(begin()==0);fill();assert(request(PP_BLE_PROGRESS,NULL,0)==0 && pp_ble_u32(out+7)==5000);
    assert(request(PP_BLE_COMMIT,NULL,0)==0 && stages==1 && !busy && !managed);
    for(unsigned op=PP_BLE_ACTIVATE;op<=PP_BLE_REMOVE;op++) assert(request(op,id,32)==0);
    assert(managed==4);
    assert(begin()==0);fill();stage_result=PP_BLE_PACKAGE;
    assert(request(PP_BLE_COMMIT,NULL,0)==PP_BLE_PACKAGE && !busy);
    stage_result=0;assert(begin()==0);pp_ble_close(&session);assert(!busy && !allocations);
    open_session();auth();assert(begin()==0);assert(request(PP_BLE_PROGRESS,NULL,0)==0 && pp_ble_u32(out+7)==0);
    pp_ble_close(&session);assert(!busy && !allocations);
}
static void test_status_and_bounds(void) {
    open_session();auth();uint8_t p[68]={0};
    assert(request(PP_BLE_STATUS,p,4)==0 && pp_ble_u32(out+7)==1050 && response_size==447);
    pp_ble_put32(p,800);assert(request(PP_BLE_STATUS,p,4)==0 && response_size==297);
    pp_ble_put32(p,1050);assert(request(PP_BLE_STATUS,p,4)==PP_BLE_OFFSET);
    pp_ble_put32(p,MODULE_SLOT_BYTES+1);memcpy(p+36,"photoframe",11);
    assert(request(PP_BLE_BEGIN,p,68)==PP_BLE_INVALID && !busy);
    pp_ble_put32(p,5000);memset(p+36,'a',32);assert(request(PP_BLE_BEGIN,p,68)==PP_BLE_INVALID);
    assert(request(255,NULL,0)==PP_BLE_INVALID);
}
static void test_fragments(void) {
    pp_ble_fragments f={0};uint8_t p[20]={0,0,35,0};memset(p+4,0xa5,16);
    assert(pp_ble_fragment(&f,p,20)==0 && f.received==16);
    assert(pp_ble_fragment(&f,p,20)==0 && f.received==16);
    p[0]=17;assert(pp_ble_fragment(&f,p,20)==-1 && f.received==0);
    p[0]=0;assert(pp_ble_fragment(&f,p,20)==0);
    p[0]=16;assert(pp_ble_fragment(&f,p,20)==0);
    p[0]=32;assert(pp_ble_fragment(&f,p,7)==1 && f.received==35);
    p[0]=0;p[2]=1;p[3]=2;assert(pp_ble_fragment(&f,p,7)==-1); /* 513 */
    assert(pp_ble_fragment(&f,p,4)==-1);
}
int main(void) {
    for(size_t i=0;i<sizeof(package);i++) package[i]=(uint8_t)i;
    test_auth();test_transfer();test_status_and_bounds();test_fragments();
    pp_ble_close(&session);assert(!busy && !allocations && releases>5);
    return 0;
}
