#include "app_button.h"
bool app_button_sample(app_button_t *s,bool pressed,bool busy,uint32_t epoch,uint64_t now) {
    if(!s->initialized) {
        *s=(app_button_t){.initialized=true,.raw=pressed,.stable=pressed,.changed=now};
        return false;
    }
    /* Capture eligibility at the first raw edge, including the debounce window. */
    if(pressed!=s->raw) {
        s->raw=pressed;s->changed=now;
        if(pressed&&!s->gesture) {
            s->gesture=true;s->epoch=epoch;s->eligible=s->armed&&!busy;
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
    if(short_press&&!fire)s->ignored++;
    s->eligible=false;s->armed=true;s->gesture=false;return fire;
}
