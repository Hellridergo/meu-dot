#ifndef DOT_UI_H
#define DOT_UI_H
#include <stdint.h>
enum dot_screen {
  PAIR,
  IDLE,
  LISTEN,
  WAITING,
  ANSWER,
  READ,
  DECIDE,
  OFFLINE,
  FAILURE,
  LINK,
  REST,
  TRANSFER,
  SETTINGS,
  BUTTONS,
  PICK_BUTTON,
  RADIO_OFF,
  LOCKED,    // 0.5.3: lock screen; only a swipe up unlocks it
  POWER_OFF, // 0.5.3: PWR held, the ring fills toward power off
  SCREEN_COUNT
};
enum dot_action {
  NONE,
  MIC_START,
  MIC_SEND,
  MIC_DISCARD,
  DISMISS,
  APPROVE,
  DECLINE,
  CHECK_LINK,
  PLAY_LAST,
  // A touch that may become hold-to-talk opens the microphone at once, so the
  // first syllable is kept. MIC_START confirms it; MIC_DISARM drops it silently.
  MIC_ARM,
  MIC_DISARM
};
#define DOT_MAX_RECORD_SECONDS 30
enum dot_shortcut {
  SHORT_TALK,
  SHORT_SETTINGS,
  SHORT_MUTE,
  SHORT_REPEAT,
  SHORT_NONE,
  SHORT_COUNT
};
typedef struct {
  uint8_t volume, brightness, bluetooth, muted, boot, power;
} DotSettings;
DotSettings dot_settings(void);
void dot_load_settings(DotSettings settings);
void dot_open_settings(unsigned now);
void dot_boot_button(int down, unsigned now);
// 0.5.3: the PWR key works like a phone's. A short press turns the screen off
// and locks it, or wakes it to the lock screen; holding it for 3 s asks to cut
// the power (dot_power_off_requested). dot_power_button is a short press.
void dot_power_key(int down, unsigned now);
void dot_power_button(unsigned now);
// 0 while the panel is dark: paint nothing and keep the brightness at 0.
int dot_display_on(void);
// The 3 s hold completed. The goodbye frame is up; the caller cuts the power.
int dot_power_off_requested(void);
void dot_playback_level(int percent);
// flags: valid, present, charging, external power. percent 255 = unavailable.
void dot_battery(unsigned flags, unsigned percent);
#define DOT_SIDE 466
void dot_init(uint16_t *pixels);
void dot_link(int paired, int online, unsigned now);
void dot_down(int x, int y, unsigned now);
void dot_move(int x, int y, unsigned now);
void dot_up(int x, int y, unsigned now);
void dot_tick(unsigned now);
void dot_paint(unsigned now);
// Areas the last dot_paint changed, as [x0, x1) x [y0, y1). Returns 0 once
// index passes the last one; a still frame has none, so nothing needs sending.
int dot_dirty(int index, int *x0, int *y0, int *x1, int *y1);
void dot_answer(const char *text, int choice, unsigned now);
void dot_error(const char *text, unsigned now);
void dot_delivered(unsigned now);
void dot_sound_level(int percent);
void dot_passkey(int key);
int dot_next_action(void);
int dot_screen_id(void);
int dot_has_motion(void);
void dot_reduce_motion(int reduced);
#endif
