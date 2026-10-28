#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool initialized,raw,stable,armed,eligible,gesture,long_eligible;
    uint32_t ignored;
    uint64_t changed,pressed;
    uint32_t epoch;
} app_button_t;
enum app_button_event {APP_BUTTON_NONE,APP_BUTTON_SHORT,APP_BUTTON_RESET};
/* RESET is an armed >=8s release REQUEST, independent of upload/display busy.
 * It closes future work; the manager waits for any current refresh to complete
 * before persistent reset. A button held throughout boot is not armed. */
enum app_button_event app_button_event(app_button_t *,bool pressed,bool busy,uint32_t epoch,uint64_t ms);
enum app_button_event app_button_event_with_display(app_button_t *,bool pressed,bool busy,bool display_busy,uint32_t epoch,uint64_t ms);
/* Pure debounce policy: pressed=true is active-low GPIO sampled by the platform. */
bool app_button_sample(app_button_t *s,bool pressed,bool busy,uint32_t epoch,uint64_t ms);
