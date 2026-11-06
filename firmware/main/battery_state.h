#pragma once
#include <stdint.h>

// AXP2101, read-only status 0x00/0x01 and fuel gauge 0xA4.
// 255 is an unavailable percentage, including operation without a battery.
enum { BAT_VALID = 1, BAT_PRESENT = 2, BAT_CHARGING = 4, BAT_EXTERNAL = 8 };
typedef struct { uint8_t flags, percent; } BatteryState;
static inline BatteryState battery_decode(uint8_t status0, uint8_t status1,
                                          uint8_t gauge) {
  BatteryState b = {BAT_VALID, 255};
  if (status0 & (1 << 5)) b.flags |= BAT_EXTERNAL;
  if (status0 & (1 << 3)) {
    b.flags |= BAT_PRESENT;
    if (((status1 >> 5) & 3) == 1) b.flags |= BAT_CHARGING;
    if (gauge <= 100) b.percent = gauge;
  }
  return b;
}
