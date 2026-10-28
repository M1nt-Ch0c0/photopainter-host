#include "pairing.h"
#include "pairing_screen.h"
#include "pairing_vector.h"
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool pp_ble_sha256(const uint8_t *p,size_t n,uint8_t out[32]){return SHA256(p,n,out)!=NULL;}
bool pp_ble_hmac(const uint8_t *k,size_t kn,const uint8_t *p,size_t n,uint8_t out[32]){unsigned z=0;return HMAC(EVP_sha256(),k,(int)kn,p,n,out,&z)&&z==32;}
bool pp_pair_random(uint8_t *out,size_t n){assert(n==32);memcpy(out,v_serverNonce,32);return true;}
bool pp_pair_keygen(uint8_t private_key[32],uint8_t public_key[65]){
    EC_KEY *k=EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);assert(k);
    BIGNUM *secret=BN_bin2bn(v_serverPrivate,32,NULL);assert(secret);
    const EC_GROUP *g=EC_KEY_get0_group(k);EC_POINT *p=EC_POINT_new(g);assert(p);
    assert(EC_POINT_mul(g,p,secret,NULL,NULL,NULL)==1);
    assert(EC_POINT_point2oct(g,p,POINT_CONVERSION_UNCOMPRESSED,public_key,65,NULL)==65);
    assert(!memcmp(public_key,v_serverPublic,65));memcpy(private_key,v_serverPrivate,32);
    EC_POINT_free(p);BN_clear_free(secret);EC_KEY_free(k);return true;
}
bool pp_pair_ecdh(const uint8_t private_key[32],const uint8_t public_key[65],uint8_t shared[32]){
    bool ok=false;EC_KEY *k=EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);if(!k)return false;
    BIGNUM *secret=BN_bin2bn(private_key,32,NULL);const EC_GROUP *g=EC_KEY_get0_group(k);EC_POINT *p=EC_POINT_new(g);
    if(secret&&p&&EC_KEY_set_private_key(k,secret)==1&&EC_POINT_oct2point(g,p,public_key,65,NULL)==1&&
       EC_POINT_is_on_curve(g,p,NULL)==1&&!EC_POINT_is_at_infinity(g,p))ok=ECDH_compute_key(shared,32,p,k,NULL)==32;
    EC_POINT_free(p);BN_clear_free(secret);EC_KEY_free(k);return ok;
}
static pp_pair_binding saved;static unsigned saves,clears;static bool fail_save,fail_clear;
static bool save(const pp_pair_binding *b){saves++;if(fail_save)return false;saved=*b;return true;}
static bool clear(void){clears++;if(fail_clear)return false;memset(&saved,0,sizeof(saved));return true;}
static const pp_pair_ops pair_ops={save,clear};
int photopainter_board_display(const uint8_t *pixels,size_t bytes){assert(pixels&&bytes==800u*480u);return 0;}
static void status(char *out,size_t size){snprintf(out,size,"{\"runtime_ready\":true}");}
static const pp_ble_ops ble_ops={.status=status};
static pp_pairing p;static pp_ble_session s;static uint8_t info[20]={2,0,0,1,2,3,4,5},out[512],request[512];
static void setup(void){memset(&saved,0,sizeof(saved));saves=clears=0;fail_save=fail_clear=false;pp_pair_init(&p,&pair_ops,v_deviceID,NULL,false);pp_ble_open(&s,&ble_ops,v_deviceID,v_challenge,1000);}
static size_t call(size_t n,uint64_t now){return pp_pair_request(&p,&s,info,request,n,now,out);}
static void commit(void){
    request[0]=PP_PAIR_COMMIT;memcpy(request+1,v_clientID,16);memcpy(request+17,v_bindingID,16);memcpy(request+33,v_clientCommit,32);
    size_t n=strlen(v_name);request[65]=(uint8_t)n;memcpy(request+66,v_name,n);
    assert(call(66+n,1100)==121&&out[5]==0&&out[7]==2);
    assert(!memcmp(out+8,v_bindingID,16));assert(!memcmp(out+24,v_serverPublic,65));assert(!memcmp(out+89,v_serverCommit,32));
    assert(p.state==PP_PAIR_WAIT_REVEAL&&!p.has_root&&!p.view_pending);
}
static void reveal(void){
    request[0]=PP_PAIR_REVEAL;memcpy(request+1,v_bindingID,16);memcpy(request+17,v_clientPublic,65);memcpy(request+82,v_clientNonce,32);
    assert(call(114,1200)==125&&out[5]==0);assert(!memcmp(out+24,v_serverNonce,32));
    assert(p.state==PP_PAIR_WAIT_CLIENT&&p.has_root&&!p.view_pending);assert(!memcmp(p.candidate.root,v_root,32));assert(p.code==314164);
    uint8_t tag[32];assert(pp_ble_tag(v_root,32,"pp-pair-v2/response",out,93,tag));assert(!memcmp(tag,out+93,32));
}
static pp_pair_view poll(void){
    request[0]=PP_PAIR_POLL;memcpy(request+1,v_bindingID,16);memcpy(request+17,v_pollProof,32);
    assert(call(49,1300)==93&&out[5]==0&&out[24]==PP_PAIR_DISPLAYING);assert(p.state==PP_PAIR_DISPLAYING);
    pp_pair_view v;assert(pp_pair_take_view(&p,&v)&&v.kind==PP_PAIR_VIEW_PROMPT&&v.code==314164);assert(!pp_pair_take_view(&p,&v));return v;
}
static void auth(void){
    request[0]=PP_BOND_AUTH;memcpy(request+1,v_clientID,16);memcpy(request+17,v_bindingID,16);memcpy(request+33,v_authNonce,32);memcpy(request+65,v_authProof,32);
    assert(call(97,2000)==59&&out[0]==PP_BOND_AUTH&&out[5]==0);assert(s.authenticated&&!memcmp(s.key,v_session,32));
    uint8_t mac[32];assert(pp_ble_tag(v_session,32,"pp-ble-v1/response",out,27,mac));assert(!memcmp(mac,out+27,32));
}
int main(int argc,char **argv){
    setup();request[0]=PP_PAIR_INFO;assert(call(1,1000)==46&&out[8]==PP_PAIR_UNBOUND&&!memcmp(out+9,v_bindingID,16));
    commit();assert(!pp_pair_confirm(&p,0,1150)&&saves==0);reveal();assert(!pp_pair_confirm(&p,0,1250)&&saves==0);
    request[0]=PP_PAIR_POLL;memcpy(request+1,v_bindingID,16);memset(request+17,0,32);assert(call(49,1250)==7&&out[5]==PP_BLE_UNAUTHORIZED&&!p.view_pending);
    pp_pair_view v=poll();assert(!pp_pair_confirm(&p,v.serial,1350));
    pp_pair_view_complete(&p,v.serial+1,true,1400);assert(p.state==PP_PAIR_DISPLAYING);
    pp_pair_view_complete(&p,v.serial,true,1400);assert(p.state==PP_PAIR_AWAIT_BUTTON);
    assert(!pp_pair_confirm(&p,v.serial-1,1500)&&saves==0);assert(pp_pair_confirm(&p,v.serial,1600)&&saves==1&&p.bound);
    assert(!memcmp(saved.root,v_root,32));assert(!pp_pair_confirm(&p,v.serial,1700)&&saves==1);auth();
    request[0]=PP_BOND_AUTH;assert(call(97,2100)==7&&out[5]==PP_BLE_UNAUTHORIZED);
    request[0]=PP_BLE_AUTH;assert(call(65,2200)==7&&out[5]==PP_BLE_UNAUTHORIZED);
    request[0]=PP_BLE_STATUS;pp_ble_put32(request+1,1);memset(request+5,0,4);assert(pp_ble_tag(v_session,32,"pp-ble-v1/request",request,9,request+9));
    assert(pp_ble_request(&s,"",info,request,41,2300,out)>39&&out[5]==0);
    pp_pair_init(&p,&pair_ops,v_deviceID,&saved,false);pp_ble_open(&s,&ble_ops,v_deviceID,v_challenge,1000);auth();
    unsigned epoch=p.authorization_epoch;assert(pp_pair_reset(&p,2400)&&p.authorization_epoch!=epoch&&!p.bound&&clears==1);
    pp_ble_open(&s,&ble_ops,v_deviceID,v_challenge,2500);request[0]=PP_BOND_AUTH;assert(call(97,2600)==7&&out[5]==PP_BLE_UNCONFIGURED);
    // Display failure and storage failure never create owner, even with a press.
    setup();commit();reveal();v=poll();pp_pair_view_complete(&p,v.serial,false,1400);assert(!pp_pair_confirm(&p,v.serial,1500)&&!p.bound&&p.state==PP_PAIR_FAULT&&saves==0);
    setup();commit();reveal();v=poll();pp_pair_view_complete(&p,v.serial,true,1400);fail_save=true;assert(!pp_pair_confirm(&p,v.serial,1500)&&!p.bound&&p.error==PP_BLE_STORAGE);
    // No nonce/key substitution after client commitment.
    setup();commit();request[0]=PP_PAIR_REVEAL;memcpy(request+1,v_bindingID,16);memcpy(request+17,v_clientPublic,65);memcpy(request+82,v_clientNonce,32);request[82]^=1;
    assert(call(114,1200)==7&&out[5]==PP_BLE_UNAUTHORIZED&&!p.has_root&&!p.bound);
    // Real OpenSSL curve validation rejects an invalid uncompressed point.
    uint8_t invalid[65]={4},shared[32];assert(!pp_pair_ecdh(v_serverPrivate,invalid,shared));
    // Expiry closes approval, keeps authenticated terminal status and rate limit.
    setup();commit();reveal();v=poll();pp_pair_view_complete(&p,v.serial,true,1400);pp_pair_tick(&p,121400);assert(p.state==PP_PAIR_EXPIRED);
    assert(!pp_pair_confirm(&p,v.serial,121401)&&saves==0);s.touched=121401;
    request[0]=PP_PAIR_POLL;memcpy(request+1,v_bindingID,16);memcpy(request+17,v_pollProof,32);assert(call(49,121402)==93&&out[24]==PP_PAIR_EXPIRED);
    // Cancellation requires root proof. The public binding ID is insufficient.
    setup();commit();reveal();request[0]=PP_PAIR_CANCEL;memcpy(request+1,v_bindingID,16);memset(request+17,0,32);assert(call(49,1250)==7&&p.state==PP_PAIR_WAIT_CLIENT);
    memcpy(request+17,v_cancelProof,32);assert(call(49,1260)==93&&p.state==PP_PAIR_CANCELLED&&!p.bound);
    // Reconnect invalidates poll proofs from an earlier challenge.
    setup();commit();reveal();uint8_t nonce[32];memcpy(nonce,v_challenge,32);nonce[31]^=1;pp_ble_open(&s,&ble_ops,v_deviceID,nonce,1300);
    request[0]=PP_PAIR_POLL;memcpy(request+1,v_bindingID,16);memcpy(request+17,v_pollProof,32);assert(call(49,1400)==7&&out[5]==PP_BLE_UNAUTHORIZED&&!p.view_pending);
    // Corrupt storage cannot fall back to a token or remote replacement.
    pp_pair_init(&p,&pair_ops,v_deviceID,NULL,true);request[0]=PP_BOND_AUTH;assert(call(97,1500)==7&&out[5]==PP_BLE_UNCONFIGURED);
    fail_clear=true;epoch=p.authorization_epoch;assert(!pp_pair_reset(&p,1600)&&p.storage_fault&&p.authorization_epoch!=epoch);
    uint8_t *pixels=malloc(800u*480u);assert(pixels);
    pp_pair_view screen={.kind=PP_PAIR_VIEW_PROMPT,.code=314164};strcpy(screen.name,"Chrome on Mac");
    assert(pp_pair_screen(&screen,pixels,800u*480u));
    for(unsigned i=0;i<800u*480u;i++)assert(pixels[i]==0||pixels[i]==1||pixels[i]==4);
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(pixels,1,800u*480u,f)==800u*480u);fclose(f);}
    for(unsigned kind=0;kind<=PP_PAIR_VIEW_ERROR;kind++){screen.kind=kind;assert(pp_pair_screen(&screen,pixels,800u*480u));}
    assert(!pp_pair_screen(&screen,pixels,0));free(pixels);
    puts("pairing: independent golden vectors, physical approval, persistence, expiry and reset passed");return 0;
}
