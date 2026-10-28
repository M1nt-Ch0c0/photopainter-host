#include "push_server.h"
#include "esp_http_server.h"
#include <assert.h>
#include <string.h>
static httpd_uri_t routes[7];static unsigned count;static const char *body;
esp_err_t httpd_start(httpd_handle_t *server,const httpd_config_t *c){assert(c->server_port==80&&c->max_uri_handlers==7);*server=routes;return 0;}
esp_err_t httpd_stop(httpd_handle_t server){assert(server==routes);return 0;}
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *r){assert(server==routes&&count<7);routes[count++]=*r;return 0;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=s;return 0;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return 0;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *b){(void)r;body=b;return 0;}
int main(void){
    assert(photopainter_push_server_start()==0&&count==7);
    assert(photopainter_push_server_start()==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<count;i++)for(unsigned authenticated=0;authenticated<2;authenticated++){
        httpd_req_t req={.authorization=authenticated?"Bearer retired-device-token-that-must-never-work":NULL,.status="200 OK"};
        assert(routes[i].handler(&req)==0&&body);
        if(!strncmp(routes[i].uri,"/api/",5)){assert(!strcmp(req.status,"410 Gone"));assert(strstr(body,"paired Bluetooth"));}
        else {assert(!strcmp(req.status,"200 OK"));assert(strstr(body,"\"protocol\":2"));}
    }
    return 0;
}
