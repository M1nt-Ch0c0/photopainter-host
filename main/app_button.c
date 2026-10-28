#include "app_button.h"
enum app_button_event app_button_event_with_display(app_button_t *s,bool pressed,bool busy,bool display_busy,uint32_t epoch,uint64_t now) {
    /* A long gesture requests revocation even while a frame is refreshing.
     * The manager closes new admission immediately and waits for the current
     * physical transaction to finish before committing the reset. */
    (void)display_busy;
    if(!s->initialized) {
        *s=(app_button_t){.initialized=true,.raw=pressed,.stable=pressed,.changed=now};
        return false;
    }
    /* Capture eligibility at the first raw edge, including the debounce window. */
    if(pressed!=s->raw) {
        s->raw=pressed;s->changed=now;
        if(pressed&&!s->gesture) {
            s->gesture=true;s->epoch=epoch;s->eligible=s->armed&&!busy;s->long_eligible=s->armed;
        }
    }
    if(s->gesture&&(busy||epoch!=s->epoch))s->eligible=false;
    if(now-s->changed<30)return false;
    if(!s->raw&&!s->stable) {
        s->armed=true;s->gesture=false;return false;
    }
    if(s->raw==s->stable)return false;
    s->stable=s->raw;
    if(s->stable) {s->pressed=now;s->armed=false;return false;}
    uint64_t duration=now-s->pressed;
    bool short_press=duration>=50&&duration<600;
    bool fire=short_press&&s->eligible&&!busy&&epoch==s->epoch;
    bool reset=duration>=8000&&s->long_eligible;
    if(short_press&&!fire)s->ignored++;
    s->eligible=s->long_eligible=false;s->armed=true;s->gesture=false;
    return reset?APP_BUTTON_RESET:fire?APP_BUTTON_SHORT:APP_BUTTON_NONE;
}
enum app_button_event app_button_event(app_button_t *s,bool pressed,bool busy,uint32_t epoch,uint64_t now) {
    return app_button_event_with_display(s,pressed,busy,busy,epoch,now);
}
bool app_button_sample(app_button_t *s,bool pressed,bool busy,uint32_t epoch,uint64_t now) {
    return app_button_event(s,pressed,busy,epoch,now)==APP_BUTTON_SHORT;
}
