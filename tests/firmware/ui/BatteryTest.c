#include <assert.h>
#include <stdio.h>
#include "battery_state.h"
int main(void) {
  BatteryState b=battery_decode(8,0x40,0);
  assert(b.flags==(BAT_VALID|BAT_PRESENT)&&b.percent==0);
  b=battery_decode(8|32,0x20,100);
  assert(b.flags==15&&b.percent==100);
  b=battery_decode(32,0x20,78);
  assert(b.flags==(BAT_VALID|BAT_EXTERNAL)&&b.percent==255);
  b=battery_decode(8|32,0x00,101);
  assert(b.flags==11&&b.percent==255);
  b=battery_decode(8|32,0x04,96);
  assert(!(b.flags&BAT_CHARGING)&&b.percent==96);
  puts("AXP2101: zero, full, absent, invalid gauge and charge-done states passed");
}
