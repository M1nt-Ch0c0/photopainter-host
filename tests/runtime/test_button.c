#include "app_button.h"
#include <assert.h>
static app_button_t b;static unsigned fires;
static void sample(bool down,bool busy,unsigned epoch,unsigned now) {
    fires+=app_button_sample(&b,down,busy,epoch,now);
}
static void reset(void){b=(app_button_t){0};fires=0;sample(false,false,0,0);sample(false,false,0,30);}
int main(void) {
    reset();sample(true,false,0,100);sample(true,false,0,130);
    sample(false,false,0,200);sample(false,false,0,230);assert(fires==1);
    sample(false,false,0,500);assert(fires==1);
    reset();sample(true,false,0,100);sample(false,false,0,110);
    sample(true,false,0,120);sample(true,false,0,150);
    sample(false,false,0,230);sample(true,false,0,240);sample(false,false,0,250);
    sample(false,false,0,280);assert(fires==1);
    reset();sample(true,false,0,100);sample(true,false,0,130);
    sample(true,false,0,1000);sample(false,false,0,1100);sample(false,false,0,1130);assert(fires==0);
    b=(app_button_t){0};fires=0;sample(true,false,0,0);sample(false,false,0,200);
    sample(false,false,0,230);assert(fires==0);
    sample(true,false,0,300);sample(true,false,0,330);sample(false,false,0,400);sample(false,false,0,430);assert(fires==1);
    reset();sample(true,true,1,100);sample(true,false,1,130);
    sample(false,false,1,200);sample(false,false,1,230);assert(fires==0&&b.ignored==1);
    reset();sample(true,false,0,100);sample(true,false,0,130);
    sample(true,true,1,150);sample(false,false,1,200);sample(false,false,1,230);assert(fires==0);
    reset();sample(true,false,0,100);sample(true,false,0,130);
    sample(false,false,1,200);sample(false,false,1,230);assert(fires==0);
    /* Timer epochs during an intentional 8-second hold do not suppress reset. */
    reset();app_button_event(&b,true,false,1,100);app_button_event(&b,true,false,1,130);
    app_button_event(&b,true,true,2,300);app_button_event(&b,true,false,3,8000);
    assert(app_button_event(&b,false,false,3,8130)==APP_BUTTON_NONE);
    assert(app_button_event(&b,false,false,3,8160)==APP_BUTTON_RESET);
    assert(app_button_event(&b,false,false,3,9000)==APP_BUTTON_NONE);
    /* Busy never prevents a physical reset request; actual reset is deferred
     * by the manager until the current display has completed safely. */
    reset();app_button_event(&b,true,true,1,100);app_button_event(&b,true,true,1,130);
    app_button_event(&b,false,false,2,8200);assert(app_button_event(&b,false,false,2,8230)==APP_BUTTON_RESET);
    reset();app_button_event(&b,true,false,1,100);app_button_event(&b,true,false,1,130);
    app_button_event(&b,false,true,2,8200);assert(app_button_event(&b,false,true,2,8230)==APP_BUTTON_RESET);
    /* Holding the function button throughout boot is not an erase gesture. */
    b=(app_button_t){0};app_button_event(&b,true,false,0,0);
    app_button_event(&b,false,false,0,9000);assert(app_button_event(&b,false,false,0,9030)==APP_BUTTON_NONE);
    /* Upload admission is busy but is not a physical-display critical section. */
    reset();app_button_event_with_display(&b,true,true,false,1,100);app_button_event_with_display(&b,true,true,false,1,130);
    app_button_event_with_display(&b,false,true,false,9,8200);
    assert(app_button_event_with_display(&b,false,true,false,9,8230)==APP_BUTTON_RESET);
    reset();app_button_event_with_display(&b,true,false,false,1,100);app_button_event_with_display(&b,true,false,false,1,130);
    app_button_event_with_display(&b,false,true,true,9,8200);
    assert(app_button_event_with_display(&b,false,false,false,9,8230)==APP_BUTTON_RESET);
    return 0;
}
