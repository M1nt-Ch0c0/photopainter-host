#include "pairing.h"
#include <string.h>
#include <limits.h>

static void wipe(void *p,size_t n){volatile uint8_t *q=p;while(n--)*q++=0;}
static bool same(const uint8_t *a,const uint8_t *b,size_t n){unsigned d=0;while(n--)d|=*a++^*b++;return d==0;}
static bool nonzero(const uint8_t *p,size_t n){unsigned v=0;while(n--)v|=*p++;return v!=0;}
static bool name_valid(const uint8_t *p,size_t n){
    if(!n||n>PP_PAIR_NAME_MAX||p[0]==' '||p[n-1]==' ')return false;
    for(size_t i=0;i<n;i++)if(p[i]<0x20||p[i]>0x7e)return false;
    return true;
}
bool pp_pair_binding_valid(const pp_pair_binding *b){
    if(!b)return false;
    size_t n=strnlen(b->name,sizeof(b->name));
    return nonzero(b->client,16)&&nonzero(b->binding,16)&&nonzero(b->root,32)&&name_valid((const uint8_t *)b->name,n);
}
static void view(pp_pairing *p,unsigned kind){
    if(++p->view_serial==0)++p->view_serial;
    p->view=(pp_pair_view){.serial=p->view_serial,.kind=kind,.code=p->code};
    const pp_pair_binding *b=p->bound?&p->owner:&p->candidate;
    memcpy(p->view.binding,b->binding,16);memcpy(p->view.name,b->name,sizeof(b->name));p->view_pending=true;
}
static void clear_candidate(pp_pairing *p){
    wipe(&p->candidate,sizeof(p->candidate));wipe(p->private_key,32);wipe(p->public_key,65);
    wipe(p->nonce,32);wipe(p->client_public,65);wipe(p->client_nonce,32);
    wipe(p->client_commit,32);wipe(p->server_commit,32);
    p->has_candidate=p->has_root=false;p->deadline=0;p->code=0;p->error=0;
}
void pp_pair_init(pp_pairing *p,const pp_pair_ops *ops,const uint8_t id[6],const pp_pair_binding *owner,bool fault){
    memset(p,0,sizeof(*p));p->ops=ops;memcpy(p->id,id,6);p->authorization_epoch=1;
    p->storage_fault=fault;
    if(owner&&pp_pair_binding_valid(owner)){p->owner=*owner;p->bound=true;}
    else if(owner)p->storage_fault=true;
    p->state=p->storage_fault?PP_PAIR_FAULT:p->bound?PP_PAIR_BOUND:PP_PAIR_UNBOUND;
}
void pp_pair_tick(pp_pairing *p,uint64_t now){
    if(p->state>=PP_PAIR_WAIT_REVEAL&&p->state<=PP_PAIR_AWAIT_BUTTON&&now>=p->deadline){
        bool visible=p->state==PP_PAIR_DISPLAYING||p->state==PP_PAIR_AWAIT_BUTTON;
        p->state=PP_PAIR_EXPIRED;p->error=PP_BLE_EXPIRED;wipe(p->private_key,32);
        if(visible)view(p,PP_PAIR_VIEW_EXPIRED);
    }
}
bool pp_pair_blocks_app(const pp_pairing *p){return p->state==PP_PAIR_DISPLAYING||p->state==PP_PAIR_AWAIT_BUTTON;}
bool pp_pair_take_view(pp_pairing *p,pp_pair_view *out){if(!p->view_pending)return false;*out=p->view;p->view_pending=false;return true;}
void pp_pair_view_complete(pp_pairing *p,uint32_t serial,bool success,uint64_t now){
    pp_pair_tick(p,now);
    if(serial!=p->view_serial||p->view.kind!=PP_PAIR_VIEW_PROMPT||p->state!=PP_PAIR_DISPLAYING)return;
    if(!success){p->state=PP_PAIR_FAULT;p->error=PP_BLE_DISPLAY_FAILED;wipe(p->private_key,32);return;}
    p->state=PP_PAIR_AWAIT_BUTTON;p->deadline=now+PP_PAIR_CONFIRM_MS;
}
bool pp_pair_confirm(pp_pairing *p,uint32_t serial,uint64_t now){
    pp_pair_tick(p,now);
    if(p->bound||p->storage_fault||p->state!=PP_PAIR_AWAIT_BUTTON||!p->has_root||serial!=p->view_serial)return false;
    if(!p->ops||!p->ops->save||!p->ops->save(&p->candidate)){
        p->state=PP_PAIR_FAULT;p->error=PP_BLE_STORAGE;view(p,PP_PAIR_VIEW_ERROR);return false;
    }
    p->owner=p->candidate;p->bound=true;p->state=PP_PAIR_BOUND;p->deadline=0;p->error=0;
    wipe(p->private_key,32);view(p,PP_PAIR_VIEW_BOUND);return true;
}
bool pp_pair_reset(pp_pairing *p,uint64_t now){
    if(++p->authorization_epoch==0)++p->authorization_epoch;
    bool ok=p->ops&&p->ops->clear&&p->ops->clear();
    wipe(&p->owner,sizeof(p->owner));p->bound=false;clear_candidate(p);p->next_start=now;
    p->storage_fault=!ok;p->state=ok?PP_PAIR_UNBOUND:PP_PAIR_FAULT;p->error=ok?0:PP_BLE_STORAGE;
    view(p,ok?PP_PAIR_VIEW_READY:PP_PAIR_VIEW_ERROR);return ok;
}
static size_t header(uint8_t *out,uint8_t op,unsigned code){out[0]=op;pp_ble_put32(out+1,0);out[5]=(uint8_t)code;out[6]=(uint8_t)(code>>8);return 7;}
static size_t signed_reply(uint8_t *out,size_t n,const uint8_t key[32],const char *domain){
    if(!pp_ble_tag(key,32,domain,out,n,out+n)){out[5]=PP_BLE_UNAUTHORIZED;return 7;}
    return n+32;
}
static uint32_t remaining(const pp_pairing *p,uint64_t now){return p->deadline>now?(uint32_t)(p->deadline-now):0;}
static bool digest(const char *domain,const uint8_t *data,size_t n,uint8_t result[32]){
    uint8_t input[512+32];size_t d=strlen(domain);if(d>32||n>512)return false;
    memcpy(input,domain,d);memcpy(input+d,data,n);bool ok=pp_ble_sha256(input,d+n,result);wipe(input,sizeof(input));return ok;
}
static size_t identity(const pp_pairing *p,const pp_pair_binding *b,uint8_t *out){
    out[0]=PP_PAIR_VERSION;memcpy(out+1,p->id,6);memcpy(out+7,b->client,16);memcpy(out+23,b->binding,16);return 39;
}
static size_t client_commit_data(const pp_pairing *p,uint8_t *out){
    size_t n=identity(p,&p->candidate,out),name=strlen(p->candidate.name);
    memcpy(out+n,p->client_public,65);n+=65;memcpy(out+n,p->client_nonce,32);n+=32;
    out[n++]=(uint8_t)name;memcpy(out+n,p->candidate.name,name);return n+name;
}
static size_t server_commit_data(const pp_pairing *p,uint8_t *out){
    size_t n=identity(p,&p->candidate,out),name=strlen(p->candidate.name);
    memcpy(out+n,p->client_commit,32);n+=32;memcpy(out+n,p->public_key,65);n+=65;memcpy(out+n,p->nonce,32);n+=32;
    out[n++]=(uint8_t)name;memcpy(out+n,p->candidate.name,name);return n+name;
}
static size_t transcript(const pp_pairing *p,uint8_t *out){
    size_t n=identity(p,&p->candidate,out),name=strlen(p->candidate.name);
    memcpy(out+n,p->client_commit,32);n+=32;memcpy(out+n,p->server_commit,32);n+=32;
    memcpy(out+n,p->client_public,65);n+=65;memcpy(out+n,p->client_nonce,32);n+=32;
    memcpy(out+n,p->public_key,65);n+=65;memcpy(out+n,p->nonce,32);n+=32;
    out[n++]=(uint8_t)name;memcpy(out+n,p->candidate.name,name);return n+name;
}
static bool derive(pp_pairing *p){
    uint8_t t[512],salt[32],shared[32],prk[32],code[32];size_t n=transcript(p,t);
    static const uint8_t expand[]="pp-pair-v2/bond\x01";
    bool ok=pp_pair_ecdh(p->private_key,p->client_public,shared)&&digest("pp-pair-v2/salt",t,n,salt)&&
        pp_ble_hmac(salt,32,shared,32,prk)&&pp_ble_hmac(prk,32,expand,sizeof(expand)-1,p->candidate.root)&&
        pp_ble_tag(p->candidate.root,32,"pp-pair-v2/code",t,n,code);
    if(ok)p->code=pp_ble_u32(code)%1000000u;
    wipe(t,sizeof(t));wipe(salt,32);wipe(shared,32);wipe(prk,32);wipe(code,32);return ok;
}
static size_t status_reply(pp_pairing *p,pp_ble_session *s,uint8_t op,uint64_t now,uint8_t *out,bool reveal){
    unsigned code=p->state==PP_PAIR_FAULT?p->error:PP_BLE_OK;
    size_t n=header(out,op,code);out[n++]=PP_PAIR_VERSION;memcpy(out+n,p->candidate.binding,16);n+=16;
    if(reveal){memcpy(out+n,p->nonce,32);n+=32;}
    out[n++]=p->state;pp_ble_put32(out+n,remaining(p,now));n+=4;memcpy(out+n,s->nonce,32);n+=32;
    return signed_reply(out,n,p->candidate.root,"pp-pair-v2/response");
}
static bool proof(const pp_pairing *p,const pp_pair_binding *b,const uint8_t challenge[32],const char *domain,const uint8_t mac[32]){
    uint8_t a[71],expected[32];size_t n=identity(p,b,a);memcpy(a+n,challenge,32);n+=32;
    bool ok=pp_ble_tag(b->root,32,domain,a,n,expected)&&same(expected,mac,32);wipe(expected,32);return ok;
}
size_t pp_pair_request(pp_pairing *p,pp_ble_session *s,const uint8_t info[20],const uint8_t *r,size_t size,uint64_t now,uint8_t out[512]){
    uint8_t op=size?r[0]:0;header(out,op,PP_BLE_INVALID);pp_pair_tick(p,now);
    if(!s->ops||pp_ble_expire(s,now))return header(out,op,PP_BLE_EXPIRED);
    if(op==PP_BLE_AUTH)return header(out,op,PP_BLE_UNAUTHORIZED);
    if(op==PP_PAIR_INFO){
        if(size!=1)return 7;
        size_t n=header(out,op,PP_BLE_OK);out[n++]=PP_PAIR_VERSION;out[n++]=p->state;
        const pp_pair_binding *b=p->bound?&p->owner:p->has_candidate?&p->candidate:NULL;
        memcpy(out+n,b?b->binding:s->nonce,16);n+=16;
        if(b)memcpy(out+n,b->client,16);else memset(out+n,0,16);n+=16;
        pp_ble_put32(out+n,remaining(p,now));n+=4;
        size_t name=b?strlen(b->name):0;out[n++]=(uint8_t)name;if(name)memcpy(out+n,b->name,name);
        s->touched=now;return n+name;
    }
    if(op==PP_BOND_AUTH){
        if(!s->challenge||s->authenticated)return header(out,op,PP_BLE_UNAUTHORIZED);
        s->challenge=false;
        if(!p->bound||p->storage_fault)return header(out,op,PP_BLE_UNCONFIGURED);
        if(size!=97||!same(r+1,p->owner.client,16)||!same(r+17,p->owner.binding,16))return header(out,op,PP_BLE_UNAUTHORIZED);
        uint8_t a[103],expected[32];size_t n=identity(p,&p->owner,a);memcpy(a+n,s->nonce,32);n+=32;memcpy(a+n,r+33,32);n+=32;
        bool ok=pp_ble_tag(p->owner.root,32,"pp-bond-v2/client",a,n,expected)&&same(expected,r+65,32)&&
            pp_ble_tag(p->owner.root,32,"pp-bond-v2/session",a,n,s->key);
        wipe(a,sizeof(a));wipe(expected,32);if(!ok)return header(out,op,PP_BLE_UNAUTHORIZED);
        s->authenticated=true;s->sequence=0;s->touched=now;header(out,op,PP_BLE_OK);memcpy(out+7,info,20);
        return signed_reply(out,27,s->key,"pp-ble-v1/response");
    }
    if(p->storage_fault)return header(out,op,PP_BLE_STORAGE);
    if(op==PP_PAIR_COMMIT){
        if(size<67||r[65]<1||r[65]>32||size!=66u+r[65]||!nonzero(r+1,16)||!nonzero(r+17,16)||!name_valid(r+66,r[65]))return 7;
        if(p->bound)return header(out,op,PP_BLE_STATE);
        if(p->has_candidate&&same(r+17,p->candidate.binding,16)){
            if(p->state<PP_PAIR_WAIT_REVEAL||p->state>PP_PAIR_AWAIT_BUTTON)return header(out,op,PP_BLE_EXPIRED);
            if(!same(r+1,p->candidate.client,16)||!same(r+33,p->client_commit,32)||strlen(p->candidate.name)!=r[65]||memcmp(r+66,p->candidate.name,r[65]))return header(out,op,PP_BLE_BUSY);
        }else{
            if(pp_pair_blocks_app(p)||(p->state==PP_PAIR_WAIT_REVEAL||p->state==PP_PAIR_WAIT_CLIENT)||now<p->next_start)return header(out,op,PP_BLE_BUSY);
            if(!same(r+17,s->nonce,16))return header(out,op,PP_BLE_INVALID);
            clear_candidate(p);p->has_candidate=true;memcpy(p->candidate.client,r+1,16);memcpy(p->candidate.binding,r+17,16);
            memcpy(p->client_commit,r+33,32);memcpy(p->candidate.name,r+66,r[65]);p->candidate.name[r[65]]=0;
            uint8_t data[512];bool ok=pp_pair_keygen(p->private_key,p->public_key)&&pp_pair_random(p->nonce,32)&&
                digest("pp-pair-v2/server-commit",data,server_commit_data(p,data),p->server_commit);
            wipe(data,sizeof(data));
            if(!ok){clear_candidate(p);p->state=PP_PAIR_FAULT;p->error=PP_BLE_UNAUTHORIZED;return header(out,op,PP_BLE_UNAUTHORIZED);}
            p->state=PP_PAIR_WAIT_REVEAL;p->deadline=now+PP_PAIR_COMMIT_MS;p->next_start=now+PP_PAIR_COOLDOWN_MS;
        }
        size_t n=header(out,op,PP_BLE_OK);out[n++]=PP_PAIR_VERSION;memcpy(out+n,p->candidate.binding,16);n+=16;
        memcpy(out+n,p->public_key,65);n+=65;memcpy(out+n,p->server_commit,32);n+=32;s->touched=now;return n;
    }
    if(op==PP_PAIR_REVEAL){
        if(size!=114||!p->has_candidate||!same(r+1,p->candidate.binding,16))return header(out,op,PP_BLE_STATE);
        if(p->state==PP_PAIR_WAIT_REVEAL){
            uint8_t data[512],expected[32];memcpy(p->client_public,r+17,65);memcpy(p->client_nonce,r+82,32);
            bool ok=r[17]==4&&digest("pp-pair-v2/client-commit",data,client_commit_data(p,data),expected)&&same(expected,p->client_commit,32)&&derive(p);
            wipe(data,sizeof(data));wipe(expected,32);
            if(!ok){p->state=PP_PAIR_CANCELLED;p->error=PP_BLE_UNAUTHORIZED;wipe(p->private_key,32);return header(out,op,PP_BLE_UNAUTHORIZED);}
            p->has_root=true;p->state=PP_PAIR_WAIT_CLIENT;p->deadline=now+PP_PAIR_COMMIT_MS;wipe(p->private_key,32);
        }else if(!p->has_root||!same(r+17,p->client_public,65)||!same(r+82,p->client_nonce,32))return header(out,op,PP_BLE_STATE);
        s->touched=now;return status_reply(p,s,op,now,out,true);
    }
    if(op==PP_PAIR_POLL||op==PP_PAIR_CANCEL){
        if(size!=49)return 7;
        bool owner=p->bound&&same(r+1,p->owner.binding,16);
        const pp_pair_binding *b=owner?&p->owner:p->has_root&&same(r+1,p->candidate.binding,16)?&p->candidate:NULL;
        if(!b||!proof(p,b,s->nonce,op==PP_PAIR_POLL?"pp-pair-v2/poll":"pp-pair-v2/cancel",r+17))return header(out,op,PP_BLE_UNAUTHORIZED);
        if(owner){
            if(op==PP_PAIR_CANCEL)return header(out,op,PP_BLE_STATE);
            size_t n=header(out,op,PP_BLE_OK);out[n++]=PP_PAIR_VERSION;memcpy(out+n,p->owner.binding,16);n+=16;
            out[n++]=PP_PAIR_BOUND;pp_ble_put32(out+n,0);n+=4;memcpy(out+n,s->nonce,32);n+=32;s->touched=now;
            return signed_reply(out,n,p->owner.root,"pp-pair-v2/response");
        }
        if(op==PP_PAIR_POLL&&p->state==PP_PAIR_WAIT_CLIENT){p->state=PP_PAIR_DISPLAYING;p->deadline=now+PP_PAIR_DISPLAY_MS;view(p,PP_PAIR_VIEW_PROMPT);}
        else if(op==PP_PAIR_CANCEL&&p->state>=PP_PAIR_WAIT_REVEAL&&p->state<=PP_PAIR_AWAIT_BUTTON){
            bool visible=pp_pair_blocks_app(p);p->state=PP_PAIR_CANCELLED;p->deadline=0;if(visible)view(p,PP_PAIR_VIEW_CANCELLED);
        }
        s->touched=now;return status_reply(p,s,op,now,out,false);
    }
    return header(out,op,PP_BLE_INVALID);
}
