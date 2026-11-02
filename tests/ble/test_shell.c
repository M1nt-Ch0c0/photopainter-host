/* Signed op 28: validation, single execution and paging over the production
 * protocol code, with a deterministic fake command table. */
#include "ble_protocol.h"
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
static uint8_t info[20]={2,2,0,1,2,3,4,5,192,168,1,42};
static const char token[]="0123456789abcdef0123456789abcdef";
static unsigned executions;
static char last_args[512];
static bool acquire(void) {assert(!"shell must not take admission");return false;}
static void release(void) {assert(!"shell must not take admission");}
static uint8_t exec(int argc,char **argv,const uint8_t in[20],pp_shell_out *o) {
    assert(argc>=1 && argc<=PP_SHELL_ARGS_MAX && !argv[argc] && in==info);executions++;
    last_args[0]=0;
    for(int i=0;i<argc;i++){strcat(last_args,argv[i]);strcat(last_args,"|");}
    if(!strcmp(argv[0],"big")) {
        long n=strtol(argv[1],NULL,10);
        for(long i=0;i<n;i++){char c=(char)('a'+i%26);pp_shell_write(o,&c,1);}
        return 0;
    }
    if(!strcmp(argv[0],"empty")) return 0;
    if(!strcmp(argv[0],"fail")) {pp_shell_printf(o,"failed %d\n",7);return PP_SHELL_EXIT_ERROR;}
    if(!strcmp(argv[0],"args")) {pp_shell_printf(o,"%d",argc);return 0;}
    pp_shell_printf(o,"%s: command not found\n",argv[0]);return PP_SHELL_EXIT_UNKNOWN;
}
static const pp_ble_ops wired={.acquire=acquire,.release=release,.shell=exec};
static const pp_ble_ops unwired={.acquire=acquire,.release=release};
static pp_ble_session session;
static uint8_t client_key[32],out[512];
static uint32_t seq;
static size_t response_size;
static const uint64_t now=10;
static void unhex(const char *p,uint8_t *o) {
    for(size_t i=0;i<32;i++) {unsigned v;assert(sscanf(p+2*i,"%2x",&v)==1);o[i]=(uint8_t)v;}
}
static void open_session(const pp_ble_ops *ops) {
    pp_ble_close(&session);uint8_t nonce[32];for(unsigned i=0;i<32;i++) nonce[i]=(uint8_t)i;
    pp_ble_open(&session,ops,info+2,nonce,now);seq=0;
    /* Same fixed vector as test_protocol.c; production uses BondAuth keys. */
    uint8_t request[65]={PP_BLE_AUTH};
    for(unsigned i=0;i<32;i++) request[1+i]=(uint8_t)(32+i);
    unhex("827d2691b1e2ac5990b86832bbaae8eaa53767b4a807d027d5a13c62cd9b495c",request+33);
    assert(pp_ble_request(&session,token,info,request,sizeof(request),now,out)==59 && out[5]==0);
    unhex("2b1310093d3f4c44d104014ce803b48f19cfefc38a66f583daf21a5a3bd6aa2e",client_key);
}
static int request(uint8_t op,const void *data,size_t n) {
    uint8_t p[512]={op};pp_ble_put32(p+1,++seq);
    assert(n+37<=sizeof(p));
    if(n) memcpy(p+5,data,n);
    assert(pp_ble_tag(client_key,32,"pp-ble-v1/request",p,n+5,p+n+5));
    response_size=pp_ble_request(&session,token,info,p,n+37,now,out);
    assert(response_size>=39 && out[0]==op && pp_ble_u32(out+1)==seq);
    uint8_t tag[32];assert(pp_ble_tag(client_key,32,"pp-ble-v1/response",out,response_size-32,tag));
    assert(!memcmp(tag,out+response_size-32,32));
    return out[5]|out[6]<<8;
}
static int shell_raw(uint32_t offset,const void *command,size_t n) {
    uint8_t p[300];assert(n+4<=sizeof(p));pp_ble_put32(p,offset);
    if(n) memcpy(p+4,command,n);
    return request(PP_BLE_SHELL,p,n+4);
}
static int shell(uint32_t offset,const char *command) {return shell_raw(offset,command,strlen(command));}
static uint32_t total(void) {return pp_ble_u32(out+7);}
static uint32_t page_offset(void) {return pp_ble_u32(out+11);}
static uint8_t exit_code(void) {return out[15];}
static uint8_t flags(void) {return out[16];}
static size_t page_bytes(void) {return response_size-39-PP_SHELL_HEADER;}
static const uint8_t *page(void) {return out+17;}

static void test_validation(void) {
    static const char *const bad[]={"","a\nb","a\rb","\x7f","\xc0\x80","\x80","\xe4\xbd",
        "\xed\xa0\x80","\xc2\x80","\xf5\x80\x80\x80","\xf4\x90\x80\x80","\xe0\x80\x80","\x1b[0m"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        assert(!pp_shell_command_valid((const uint8_t *)bad[i],strlen(bad[i])));
    assert(!pp_shell_command_valid((const uint8_t *)"a\0b",3));
    static const char *const good[]={"help","a\tb"," ","\xe4\xb8\xad\xe6\x96\x87","\xf0\x9f\x98\x80","\xc2\xa0"};
    for(size_t i=0;i<sizeof(good)/sizeof(good[0]);i++)
        assert(pp_shell_command_valid((const uint8_t *)good[i],strlen(good[i])));
    char long_command[PP_SHELL_COMMAND_MAX+1];memset(long_command,'x',sizeof(long_command));
    assert(pp_shell_command_valid((const uint8_t *)long_command,PP_SHELL_COMMAND_MAX));
    assert(!pp_shell_command_valid((const uint8_t *)long_command,PP_SHELL_COMMAND_MAX+1));

    open_session(&wired);
    for(size_t n=0;n<4;n++){uint8_t p[4]={0};assert(request(PP_BLE_SHELL,p,n)==PP_BLE_INVALID);}
    assert(shell(0,"")==PP_BLE_INVALID);
    assert(shell_raw(0,"a\0b",3)==PP_BLE_INVALID && shell(0,"help\n")==PP_BLE_INVALID);
    assert(shell_raw(0,long_command,sizeof(long_command))==PP_BLE_INVALID);
    assert(!executions);
    assert(shell_raw(0,long_command,PP_SHELL_COMMAND_MAX)==PP_BLE_OK && executions==1);
    assert(exit_code()==PP_SHELL_EXIT_UNKNOWN);
    executions=0;
    /* Unknown ops keep the old default so clients can detect support. */
    assert(request(29,NULL,0)==PP_BLE_INVALID);
    open_session(&unwired);
    assert(shell(0,"help")==PP_BLE_STATE && shell(1,"")==PP_BLE_STATE);
    {uint8_t p[2]={0};assert(request(PP_BLE_SHELL,p,2)==PP_BLE_INVALID);}
}
static void test_paging(void) {
    open_session(&wired);
    assert(shell(1,"")==PP_BLE_OFFSET && !executions); /* No exec yet in this session. */
    assert(shell(0,"big 1000")==PP_BLE_OK && executions==1);
    assert(total()==1000 && page_offset()==0 && exit_code()==0 && flags()==0);
    assert(page_bytes()==400 && response_size==449 && page()[0]=='a' && page()[399]=='a'+399%26);
    assert(shell(400,"")==PP_BLE_OK && page_offset()==400 && page_bytes()==400 && page()[0]=='a'+400%26);
    assert(shell(999,"")==PP_BLE_OK && page_bytes()==1 && page()[0]=='a'+999%26);
    assert(shell(1000,"")==PP_BLE_OFFSET && shell(UINT32_MAX,"")==PP_BLE_OFFSET);
    assert(shell(400,"big 1")==PP_BLE_INVALID); /* Page reads carry no command. */
    assert(executions==1);
    /* A rejected exec leaves the previous capture intact. */
    assert(shell(0,"bad\x01")==PP_BLE_INVALID && shell(800,"")==PP_BLE_OK && page_bytes()==200);
    assert(executions==1);

    assert(shell(0,"big 5000")==PP_BLE_OK && total()==PP_SHELL_OUTPUT_MAX && flags()==PP_SHELL_TRUNCATED);
    assert(shell(4000,"")==PP_BLE_OK && page_bytes()==95 && flags()==PP_SHELL_TRUNCATED);
    assert(shell(4095,"")==PP_BLE_OFFSET);
    assert(shell(0,"big 4095")==PP_BLE_OK && total()==4095 && flags()==0);

    assert(shell(0,"empty")==PP_BLE_OK && total()==0 && page_bytes()==0 && response_size==49);
    assert(shell(1,"")==PP_BLE_OFFSET);
    assert(shell(0,"fail")==PP_BLE_OK && exit_code()==PP_SHELL_EXIT_ERROR && total()==9);
    assert(!memcmp(page(),"failed 7\n",9));
    assert(shell(0,"nope")==PP_BLE_OK && exit_code()==PP_SHELL_EXIT_UNKNOWN);

    unsigned before=executions;
    assert(shell(0," \t ")==PP_BLE_OK && exit_code()==PP_SHELL_EXIT_USAGE && executions==before);
    assert(shell(0,"args 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16")==PP_BLE_OK);
    assert(exit_code()==PP_SHELL_EXIT_USAGE && executions==before);
    assert(shell(0,"args 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15")==PP_BLE_OK && !memcmp(page(),"16",2));
    assert(shell(0,"  args\ta  \t b ")==PP_BLE_OK && !strcmp(last_args,"args|a|b|"));
    assert(shell(0,"args \xe4\xb8\xad")==PP_BLE_OK && !strcmp(last_args,"args|\xe4\xb8\xad|"));

    /* Captures belong to the session; a new connection starts empty. */
    assert(shell(0,"big 1000")==PP_BLE_OK);
    open_session(&wired);assert(shell(400,"")==PP_BLE_OFFSET);
    pp_ble_close(&session);
}
static void test_writer(void) {
    char data[9];pp_shell_out o={.data=data,.cap=8};
    pp_shell_printf(&o,"%s","abc");assert(o.len==3 && !o.truncated);
    pp_shell_printf(&o,"%d",123456);assert(o.len==8 && o.truncated && !memcmp(data,"abc12345",8));
    o.truncated=false;pp_shell_write(&o,"x",1);assert(o.len==8 && o.truncated);
    pp_shell_printf(&o,"y");assert(o.len==8);
}
int main(void) {
    test_writer();test_validation();test_paging();
    puts("shell ok");return 0;
}
