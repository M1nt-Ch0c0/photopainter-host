/* v2 ownership is established by physical BLE pairing. Legacy bearer-token
 * endpoints are explicitly retired; an old token must never bypass unbinding. */
#include "push_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
static bool started;
static esp_err_t retired(httpd_req_t *request){
    httpd_resp_set_status(request,"410 Gone");
    httpd_resp_set_type(request,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    httpd_resp_set_hdr(request,"Connection","close");
    return httpd_resp_sendstr(request,"Device control uses physically paired Bluetooth.\n");
}
static esp_err_t health(httpd_req_t *request){
    httpd_resp_set_type(request,"application/json");httpd_resp_set_hdr(request,"Cache-Control","no-store");
    return httpd_resp_sendstr(request,"{\"device\":\"PhotoPainter\",\"protocol\":2,\"transport\":\"paired-ble\"}");
}
esp_err_t photopainter_push_server_start(void){
    if(started)return ESP_ERR_INVALID_STATE;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.server_port=80;config.stack_size=4096;
    config.max_uri_handlers=7;config.max_open_sockets=3;config.recv_wait_timeout=5;config.send_wait_timeout=5;config.lru_purge_enable=true;
    httpd_handle_t server=NULL;esp_err_t err=httpd_start(&server,&config);if(err!=ESP_OK)return err;
    const httpd_uri_t routes[]={
        {.uri="/health",.method=HTTP_GET,.handler=health},
        {.uri="/",.method=HTTP_GET,.handler=health},
        {.uri="/api/push",.method=HTTP_POST,.handler=retired},
        {.uri="/api/module",.method=HTTP_GET,.handler=retired},
        {.uri="/api/module",.method=HTTP_POST,.handler=retired},
        {.uri="/api/wifi",.method=HTTP_GET,.handler=retired},
        {.uri="/api/wifi",.method=HTTP_POST,.handler=retired},
    };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++)if((err=httpd_register_uri_handler(server,&routes[i]))!=ESP_OK){httpd_stop(server);return err;}
    started=true;ESP_LOGI("push","paired BLE control active; legacy HTTP control retired");return ESP_OK;
}
