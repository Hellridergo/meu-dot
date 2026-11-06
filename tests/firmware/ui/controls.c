#include <assert.h>
#include <stdio.h>
#include "dot_ui.h"
static uint16_t pixels[466*466];
static unsigned t=300;
static void tap(int x,int y){dot_down(x,y,t);dot_up(x,y,t+20);t+=120;}
static void down_menu(void){dot_down(230,40,t);dot_move(230,153,t+30);dot_up(230,153,t+55);t+=70;assert(dot_screen_id()==SETTINGS);}
static void swipe_up(void){dot_down(233,360,t);dot_move(233,300,t+60);dot_up(233,230,t+120);t+=200;}
int main(void){
 dot_init(pixels);dot_link(1,1,t);assert(dot_screen_id()==IDLE);down_menu();
 tap(270,158);tap(98,213);assert(dot_settings().volume==63&&dot_settings().brightness==5);
 tap(280,280);assert(!dot_settings().bluetooth && dot_screen_id()==SETTINGS);
 dot_link(0,0,t);tap(233,400);assert(dot_screen_id()==RADIO_OFF);
 tap(233,380);tap(285,281);assert(dot_settings().bluetooth);tap(233,400);
 dot_link(1,1,t);down_menu();tap(227,342);assert(dot_screen_id()==BUTTONS);
 // 0.5.3: the PWR row is fixed; only BOOT opens the picker.
 tap(222,260);assert(dot_screen_id()==BUTTONS&&dot_settings().power==SHORT_SETTINGS);
 tap(222,190);assert(dot_screen_id()==PICK_BUTTON);tap(222,250);assert(dot_settings().boot==SHORT_MUTE);
 tap(232,403);tap(232,403);dot_boot_button(1,t);dot_boot_button(0,t+30);assert(dot_settings().muted);
 down_menu();tap(227,342);tap(222,190);tap(222,140);assert(dot_settings().boot==SHORT_TALK);
 tap(232,403);tap(232,403);assert(dot_screen_id()==IDLE);
 dot_boot_button(1,t);assert(dot_screen_id()==LISTEN&&dot_next_action()==MIC_START);
 dot_tick(t+29999);assert(dot_screen_id()==LISTEN);dot_tick(t+30000);
 assert(dot_next_action()==MIC_SEND && dot_screen_id()==TRANSFER);
 dot_boot_button(0,t+30002);assert(dot_next_action()==NONE);t+=30100;
 down_menu();dot_delivered(t);tap(230,400);assert(dot_screen_id()==WAITING);
 down_menu();dot_answer("Posso concluir?",1,t);assert(dot_screen_id()==SETTINGS);
 tap(232,399);assert(dot_screen_id()==DECIDE);tap(310,379);assert(dot_next_action()==APPROVE);
 assert(dot_screen_id()==IDLE&&dot_display_on());

 // 0.5.3: dim at 15 s, dark and locked at 30 s, counted from the last tap's release.
 unsigned b=t-100;
 dot_tick(b+14999);assert(dot_screen_id()==IDLE);dot_tick(b+15000);assert(dot_screen_id()==REST);
 dot_tick(b+29999);assert(dot_display_on());
 dot_tick(b+30000);assert(!dot_display_on()&&dot_screen_id()==LOCKED);t=b+30100;
 // Dark: touches do nothing at all.
 tap(233,300);dot_down(233,300,t);dot_tick(t+700);dot_up(233,300,t+800);t+=900;
 assert(!dot_display_on()&&dot_next_action()==NONE);
 // BOOT only wakes the lock screen; neither the press nor its release records.
 dot_boot_button(1,t);assert(dot_display_on()&&dot_screen_id()==LOCKED&&dot_next_action()==NONE);
 dot_boot_button(0,t+40);assert(dot_screen_id()==LOCKED&&dot_next_action()==NONE);
 // Lit but locked: a hold, a tap and the settings swipe do nothing.
 dot_down(233,300,t);dot_tick(t+600);dot_up(233,300,t+700);t+=800;
 assert(dot_screen_id()==LOCKED&&dot_next_action()==NONE);
 dot_down(230,40,t);dot_move(230,153,t+30);dot_up(230,153,t+55);t+=70;assert(dot_screen_id()==LOCKED);
 // A short swipe snaps back; a long one unlocks.
 dot_down(233,360,t);dot_move(233,320,t+40);dot_up(233,300,t+80);t+=100;assert(dot_screen_id()==LOCKED);
 swipe_up();assert(dot_screen_id()==IDLE&&dot_display_on());
 // PWR: a short press locks at once; a reply wakes the lock screen, which goes
 // dark again after 10 s.
 dot_power_button(t);assert(!dot_display_on()&&dot_screen_id()==LOCKED);t+=200;
 dot_answer("Chegou",0,t);assert(dot_display_on()&&dot_screen_id()==LOCKED);
 dot_tick(t+9999);assert(dot_display_on());dot_tick(t+10000);assert(!dot_display_on());t+=10100;
 dot_power_button(t);assert(dot_display_on());t+=100;
 dot_power_button(t);assert(!dot_display_on());t+=100;
 dot_power_button(t);assert(dot_display_on());t+=100;
 swipe_up();assert(dot_screen_id()==ANSWER);
 // BOOT pressed while locked and released after unlocking does not record.
 dot_power_button(t);t+=100;dot_power_button(t);t+=100;dot_boot_button(1,t);swipe_up();
 dot_boot_button(0,t);assert(dot_screen_id()==ANSWER&&dot_next_action()==NONE);
 // PWR held: the power ring at 0.6 s, cancelled on release before 3 s.
 dot_power_key(1,t);dot_tick(t+599);assert(dot_screen_id()==ANSWER);
 dot_tick(t+600);assert(dot_screen_id()==POWER_OFF);
 dot_power_key(0,t+1500);assert(dot_screen_id()==ANSWER&&!dot_power_off_requested());t+=1600;
 // From the dark lock screen too: a cancelled hold returns to the dark.
 dot_power_button(t);assert(!dot_display_on());t+=100;
 dot_power_key(1,t);dot_tick(t+700);assert(dot_display_on()&&dot_screen_id()==POWER_OFF);
 dot_power_key(0,t+900);assert(!dot_display_on()&&dot_screen_id()==LOCKED);t+=1000;
 // Held to 3 s: power off is requested and stays requested.
 dot_power_key(1,t);dot_tick(t+700);dot_tick(t+3000);assert(dot_power_off_requested());
 dot_power_key(0,t+3100);assert(dot_screen_id()==POWER_OFF&&dot_power_off_requested());
 puts("30 s exatos; atalhos, volume, brilho, BLE, decisão; tela apagada, bloqueio, PWR e desligar: OK");
}
