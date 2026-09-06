#include "wifi_station.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static jmp_buf end;
static void (*worker)(void *);
static void (*event)(void *, esp_event_base_t, int32_t, void *);
static EventBits_t bits;
static int scenario, attempts, delays, started;
static int indexes[8], deadlines[8], choices[8];
static const int limit = 5;
EventGroupHandle_t xEventGroupCreate(void) { return &bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t h, EventBits_t b) { (void)h; return bits |= b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t h, EventBits_t b) { (void)h; return bits &= ~b; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t b, int clear, int all, uint32_t timeout)
{
    (void)h; (void)clear; (void)all;
    if (b == (BIT0 | BIT1)) {
        deadlines[attempts-1] = timeout;
        if (choices[attempts-1] == 1) {
            ip_event_got_ip_t ip = {0};
            event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
        } else if (choices[attempts-1] == 2) event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    } else if (b == BIT1) {
        if (scenario != 2) longjmp(end, 1);
        event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    } else if (b == BIT2) assert(bits & BIT2);
    return bits;
}
int xTaskCreate(void (*fn)(void *), const char *n, int stack, void *arg, int prio, void *out)
{ (void)n; (void)stack; (void)arg; (void)prio; (void)out; worker = fn; return pdPASS; }
void vTaskDelay(uint32_t ticks) { assert(ticks == 5000); ++delays; }
esp_err_t esp_netif_init(void) { return 0; }
void *esp_netif_create_default_wifi_sta(void) { return &bits; }
esp_err_t esp_event_loop_create_default(void) { return 0; }
esp_err_t esp_event_handler_register(esp_event_base_t b, int32_t id, void (*fn)(void *, esp_event_base_t, int32_t, void *), void *a)
{ (void)b; (void)id; (void)a; event = fn; return 0; }
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return 0; }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage == WIFI_STORAGE_RAM); return 0; }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_STA); return 0; }
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *c)
{
    (void)iface; assert(!started);
    if (attempts == limit) longjmp(end, 1);
    indexes[attempts++] = c->sta.ssid[0] - '0';
    return 0;
}
esp_err_t esp_wifi_start(void) { assert(!started); started = 1; return 0; }
esp_err_t esp_wifi_connect(void) { assert(started); return 0; }
esp_err_t esp_wifi_stop(void) { assert(started); started = 0; event(NULL, WIFI_EVENT, WIFI_EVENT_STA_STOP, NULL); return 0; }
esp_err_t esp_wifi_set_ps(int mode) { assert(mode == WIFI_PS_NONE); return 0; }
int main(int argc, char **argv)
{
    assert(argc == 2); scenario = atoi(argv[1]);
    wifi_profiles_t profiles = {.version=1, .count=3};
    for (int i=0; i<3; ++i) { profiles.items[i].ssid[0] = '0'+i; strcpy(profiles.items[i].password, "password"); }
    wifi_profiles_t before = profiles;
    if (scenario == 1) { choices[0] = 2; choices[1] = 1; }
    if (scenario == 2) { choices[0] = 2; choices[1] = 1; }
    assert(photopainter_wifi_connect(&profiles) == 0);
    if (!setjmp(end)) worker(NULL);
    assert(!memcmp(&before, &profiles, sizeof(profiles)));
    printf("%d", delays);
    for (int i=0; i<attempts; ++i) printf(" %d:%d", indexes[i], deadlines[i]);
    puts("");
    return 0;
}
