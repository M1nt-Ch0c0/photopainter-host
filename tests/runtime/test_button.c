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
    return 0;
}
