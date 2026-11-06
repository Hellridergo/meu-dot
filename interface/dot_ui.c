/* The badge and the browser compile this same RGB565 compositor and interaction
 * model. Visual direction "Lume": the Dot is the light source of the screen. */
#include "dot_ui.h"
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#endif

typedef struct {
  uint32_t offset;
  uint8_t w, h, advance;
  int8_t left, top;
} Glyph;
typedef struct {
  const Glyph *glyph;
  const uint8_t *alpha;
} Font;
typedef struct {
  const uint16_t *pixels; // premultiplied over black
  const uint8_t *alpha;   // 4-bit coverage, two pixels per byte
  uint16_t w, h;
} Sprite;
#include "art_data.inc"

#define RGB(r, g, b) (((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3))
enum {
  BG = RGB(0, 0, 0),
  INK = RGB(255, 245, 232),
  SOFT = RGB(166, 154, 140),
  FAINT = RGB(94, 85, 75),
  EDGE = RGB(42, 36, 30),
  RING = RGB(30, 26, 22),
  PLATE = RGB(28, 24, 20),
  AMBER = RGB(249, 157, 34),
  AMBER_TOP = RGB(255, 181, 71),
  AMBER_LOW = RGB(238, 143, 20),
  AMBER_DEEP = RGB(201, 112, 12),
  AMBER_SHINE = RGB(255, 214, 150),
  AMBER_HEAD = RGB(255, 208, 138),
  ON_AMBER = RGB(31, 18, 5),
  MINT = RGB(127, 220, 164),
  CORAL = RGB(255, 123, 94),
  CORAL_DEEP = RGB(196, 74, 50)
};
static uint16_t *fb;
// Paint clip: drawing below only touches [clip_x0, clip_x1) x [clip_y0, clip_y1).
static int clip_x0, clip_y0, clip_x1 = DOT_SIDE, clip_y1 = DOT_SIDE;
static void paint_reset(void);
static struct {
  enum dot_screen screen, before_link;
  unsigned started, last, press_at, anim_epoch;
  int paired, online, down, x0, y0, cancelled, hold_armed, level, quiet, key;
  int page, pages, offsets[48], has_answer, choice;
  int boot_down, hardware_talk, editing_power, playback_level, preroll;
  enum dot_screen before_settings;
  // 0.5.3 lock and power. `touched` is the last activity of the user or of the
  // Dot; the automatic dimming to REST does not count as activity.
  int dark, locked, swipe, power_down, power_off, power_was_dark, boot_swallow;
  unsigned touched, power_at, before_power_started;
  enum dot_screen before_lock, before_power;
  enum dot_action actions[8];
  int head, tail;
  char reply[1100], problem[240];
} ui;
// Dim after 15 s, then dark and locked at 30 s. A lock screen woken by a button
// or a reply goes dark again after 10 s. PWR shows the power ring after 0.6 s
// and cuts the power at 3 s.
enum {
  DIM_MS = 15000,
  DARK_MS = 30000,
  LOCK_LIT_MS = 10000,
  POWER_SHOW_MS = 600,
  POWER_OFF_MS = 3000
};
static DotSettings preferences = {58, 72, 1, 0, SHORT_TALK, SHORT_SETTINGS};
static unsigned battery_flags, battery_percent = 255;
void dot_battery(unsigned flags, unsigned percent) {
  battery_flags = flags;
  battery_percent = (flags & 3) == 3 && percent <= 100 ? percent : 255;
}
DotSettings dot_settings(void) { return preferences; }
void dot_load_settings(DotSettings value) {
  if (value.volume <= 100 && value.brightness >= 5 && value.brightness <= 100 &&
      value.bluetooth <= 1 && value.muted <= 1 && value.boot < SHORT_COUNT &&
      value.power < SHORT_COUNT)
    preferences = value;
}
void dot_playback_level(int percent) { ui.playback_level = percent; }
static int is_menu(void) {
  return ui.screen == SETTINGS || ui.screen == BUTTONS ||
         ui.screen == PICK_BUTTON;
}
static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }
static int absi(int a) { return a < 0 ? -a : a; }
static void copy_text(char *out, int cap, const char *in) {
  int n = 0;
  while (in && in[n] && n < cap - 1) {
    out[n] = in[n];
    n++;
  }
  if (in && in[n])
    while (n && ((unsigned char)in[n] & 0xc0) == 0x80)
      n--;
  out[n] = 0;
}
static int cp(const char *s, int *step) {
  unsigned a = (unsigned char)s[0];
  *step = 1;
  if (a < 128)
    return a;
  unsigned b = (unsigned char)s[1];
  if ((a & 0xe0) == 0xc0 && (b & 0xc0) == 0x80) {
    *step = 2;
    return ((a & 31) << 6) | (b & 63);
  }
  if ((a & 0xf0) == 0xe0 && b && s[2]) {
    *step = 3;
    return ((a & 15) << 12) | ((b & 63) << 6) | ((unsigned char)s[2] & 63);
  }
  if ((a & 0xf8) == 0xf0 && b && s[2] && s[3])
    *step = 4;
  return '?';
}
static int glyph_i(int c) {
  if (c == 0x2013 || c == 0x2014)
    c = '-';
  if (c == 0x201c || c == 0x201d)
    c = '"';
  if (c == 0x2019)
    c = 39;
  if (c >= 32 && c <= 255)
    return c - 32;
  return '?' - 32;
}
static uint16_t blend(uint16_t a, uint16_t b, int v) {
  int k = 15 - v;
  int r = ((a >> 11) * k + (b >> 11) * v + 7) / 15;
  int g = (((a >> 5) & 63) * k + ((b >> 5) & 63) * v + 7) / 15;
  int z = ((a & 31) * k + (b & 31) * v + 7) / 15;
  return (uint16_t)((r << 11) | (g << 5) | z);
}
static void span(int x, int y, int w, uint16_t col) {
  if (y < clip_y0 || y >= clip_y1)
    return;
  int end = min(clip_x1, x + w);
  x = max(clip_x0, x);
  uint16_t *line = fb + y * DOT_SIDE;
  for (; x < end; x++)
    line[x] = col;
}
static int text_width(const Font *f, const char *s, int len, int track) {
  int width = 0;
  for (int i = 0; s[i] && i < len;) {
    int step;
    int g = glyph_i(cp(s + i, &step));
    width += f->glyph[g].advance + track;
    i += step;
  }
  return width ? width - track : 0;
}
static void text_at(const Font *f, int x, int base, const char *s, int len,
                    uint16_t col, int track) {
  if (base + 24 < clip_y0 || base - 64 >= clip_y1)
    return;
  for (int i = 0; s[i] && i < len;) {
    int step;
    const Glyph *g = f->glyph + glyph_i(cp(s + i, &step));
    i += step;
    for (int y = 0; y < g->h; y++) {
      int py = base + g->top + y;
      if (py < clip_y0 || py >= clip_y1)
        continue;
      for (int xx = 0; xx < g->w; xx++) {
        int at = y * g->w + xx, a = f->alpha[g->offset + at / 2];
        a = (at & 1) ? (a & 15) : (a >> 4);
        int px = x + g->left + xx;
        if (a && px >= clip_x0 && px < clip_x1) {
          uint16_t *p = fb + py * DOT_SIDE + px;
          *p = a == 15 ? col : blend(*p, col, a);
        }
      }
    }
    x += g->advance + track;
  }
}
static void center(const Font *f, int base, const char *s, uint16_t c,
                   int track) {
  text_at(f, (DOT_SIDE - text_width(f, s, 4000, track)) / 2, base, s, 4000, c,
          track);
}
static int line_end(const char *s, int at, int w) {
  int i = at, space = -1, width = 0;
  for (; s[i] && s[i] != '\n';) {
    int n;
    int adv = body.glyph[glyph_i(cp(s + i, &n))].advance;
    if (width + adv > w && i > at)
      return space > at ? space : i;
    if (s[i] == ' ')
      space = i;
    width += adv;
    i += n;
  }
  return i;
}
static int skip(const char *s, int i) {
  while (s[i] == ' ' || s[i] == '\n')
    i++;
  return i;
}
static void paginate(void) {
  int i = 0;
  ui.pages = 0;
  do {
    ui.offsets[ui.pages++] = i;
    for (int j = 0; j < 5 && ui.reply[i]; j++) {
      int end = line_end(ui.reply, i, 292);
      i = skip(ui.reply, end);
    }
  } while (ui.reply[i] && ui.pages < 48);
  ui.page = 0;
}
static void num(char *out, int n) {
  int i = 0;
  char tmp[12];
  do {
    tmp[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  for (int j = 0; j < i; j++)
    out[j] = tmp[i - j - 1];
  out[i] = 0;
}
static void emit(enum dot_action a);
// Drop a microphone opened at touch-down that did not become a recording.
static void disarm(void) {
  if (ui.preroll) {
    ui.preroll = 0;
    emit(MIC_DISARM);
  }
}
static void change(enum dot_screen s, unsigned now) {
  ui.screen = s;
  if (s != LISTEN)
    ui.hardware_talk = 0;
  ui.started = now;
  ui.last = now;
  if (s != REST)
    ui.touched = now;
  ui.swipe = 0;
  ui.down = 0;
  ui.hold_armed = 0;
  disarm();
  ui.cancelled = 0;
  ui.level = 0;
}
static enum dot_screen reply_screen(void) {
  if (!ui.choice)
    return ANSWER;
  int second = skip(ui.reply, line_end(ui.reply, 0, 313));
  int tail = skip(ui.reply, line_end(ui.reply, second, 313));
  return ui.reply[tail] ? READ : DECIDE;
}
static enum dot_screen home(void) {
  return !preferences.bluetooth ? RADIO_OFF
         : !ui.paired           ? PAIR
         : !ui.online           ? OFFLINE
         : ui.has_answer        ? reply_screen()
                                : IDLE;
}
static void emit(enum dot_action a) {
  int n = (ui.tail + 1) % 8;
  if (n != ui.head) {
    ui.actions[ui.tail] = a;
    ui.tail = n;
  }
}
int dot_next_action(void) {
  if (ui.tail == ui.head)
    return NONE;
  int a = ui.actions[ui.head];
  ui.head = (ui.head + 1) % 8;
  return a;
}
int dot_screen_id(void) { return ui.screen; }
int dot_has_motion(void) {
  return !ui.quiet && ui.screen != READ && ui.screen != LINK;
}
void dot_reduce_motion(int v) { ui.quiet = v; }

/* 0.5.3: screen off and lock. While dark, touches are ignored and a button only
 * wakes the lock screen; on the lock screen only a swipe up unlocks, so a
 * pressed pocket can neither record nor answer a decision. */
static int may_dark(void) {
  return ui.screen != LISTEN && ui.screen != TRANSFER && ui.screen != WAITING &&
         ui.screen != LOCKED && ui.screen != POWER_OFF &&
         !(ui.screen == PAIR && ui.key >= 0);
}
static void lock_screen(unsigned now) {
  if (ui.screen == LISTEN)
    return;
  disarm();
  if (ui.screen != LOCKED && ui.screen != POWER_OFF)
    ui.before_lock = ui.screen;
  ui.locked = 1;
  change(LOCKED, now);
  ui.dark = 1;
}
static void wake_locked(unsigned now) {
  change(LOCKED, now);
  ui.dark = 0;
}
static void unlock(unsigned now) {
  ui.locked = 0;
  ui.dark = 0;
  enum dot_screen dest = home();
  if (dest == IDLE && (ui.before_lock == WAITING || ui.before_lock == TRANSFER ||
                       ui.before_lock == FAILURE))
    dest = ui.before_lock;
  change(dest, now);
}
int dot_display_on(void) { return !ui.dark; }
int dot_power_off_requested(void) { return ui.power_off; }

void dot_delivered(unsigned now) {
  if (ui.locked && ui.before_lock == TRANSFER)
    ui.before_lock = WAITING;
  if (ui.screen == POWER_OFF && ui.before_power == TRANSFER)
    ui.before_power = WAITING;
  if (ui.screen == TRANSFER)
    change(WAITING, now);
  else if (is_menu() && ui.before_settings == TRANSFER)
    ui.before_settings = WAITING;
}
void dot_sound_level(int level) { ui.level = max(0, min(100, level)); }
void dot_passkey(int k) { ui.key = k; }
void dot_init(uint16_t *pixels) {
  fb = pixels;
  paint_reset();
  ui.key = -1;
  ui.paired = 0;
  ui.online = 0;
  ui.has_answer = 0;
  ui.head = ui.tail = 0;
  ui.dark = ui.locked = ui.power_down = ui.power_off = ui.boot_swallow = 0;
  change(home(), 0);
}
void dot_link(int paired, int online, unsigned now) {
  if (paired == ui.paired && online == ui.online)
    return;
  ui.paired = paired;
  ui.online = online;
  if (is_menu() || ui.locked || ui.screen == POWER_OFF)
    return;
  if (!paired || !online) {
    if (ui.screen == LISTEN)
      emit(MIC_DISCARD);
    change(home(), now);
  } else if (ui.screen == PAIR || ui.screen == OFFLINE || ui.screen == LINK ||
             ui.screen == RADIO_OFF)
    change(home(), now);
}
void dot_answer(const char *t, int choice, unsigned now) {
  if (!t || !*t)
    return;
  if (ui.screen == LISTEN)
    emit(MIC_DISCARD);
  copy_text(ui.reply, sizeof(ui.reply), t);
  ui.has_answer = 1;
  ui.choice = choice;
  paginate();
  if (ui.screen == POWER_OFF) {
    // PWR is being held. If the hold is released, show the reply.
    if (ui.locked)
      ui.power_was_dark = 0;
    else {
      ui.before_power = reply_screen();
      ui.before_power_started = now;
    }
    return;
  }
  if (ui.locked) {
    // The voice plays; the screen lights up locked with "Nova resposta".
    if (ui.dark)
      wake_locked(now);
    else
      ui.touched = now;
    return;
  }
  if (is_menu())
    return;
  change(reply_screen(), now);
}
void dot_error(const char *msg, unsigned now) {
  if (ui.screen == LISTEN)
    emit(MIC_DISCARD);
  copy_text(ui.problem, sizeof(ui.problem), msg);
  if (ui.locked) {
    ui.before_lock = FAILURE;
    return;
  }
  if (ui.screen == POWER_OFF) {
    ui.before_power = FAILURE;
    ui.before_power_started = now;
    return;
  }
  if (is_menu())
    return;
  change(FAILURE, now);
}
void dot_open_settings(unsigned now) {
  if (ui.screen == LISTEN || ui.locked || ui.screen == POWER_OFF)
    return;
  if (!is_menu())
    ui.before_settings = ui.screen;
  change(SETTINGS, now);
}
static void close_settings(unsigned now) {
  enum dot_screen dest = home();
  if (dest == IDLE &&
      (ui.before_settings == WAITING || ui.before_settings == TRANSFER))
    dest = ui.before_settings;
  change(dest, now);
}
static void begin_hardware_talk(unsigned now, int source) {
  if ((ui.screen != IDLE && ui.screen != REST) || !preferences.bluetooth ||
      !ui.online)
    return;
  change(LISTEN, now);
  ui.hardware_talk = source;
  emit(MIC_START);
}
static void end_hardware_talk(unsigned now) {
  ui.hardware_talk = 0;
  if (ui.screen != LISTEN)
    return;
  if (now - ui.started < 250) {
    emit(MIC_DISCARD);
    change(home(), now);
  } else {
    emit(MIC_SEND);
    change(TRANSFER, now);
  }
}
static void shortcut(unsigned key, unsigned now) {
  if (key == SHORT_SETTINGS) {
    if (is_menu())
      close_settings(now);
    else
      dot_open_settings(now);
  } else if (key == SHORT_MUTE) {
    preferences.muted = !preferences.muted;
  } else if (key == SHORT_REPEAT && ui.screen != LISTEN) {
    if (ui.reply[0])
      emit(PLAY_LAST);
  }
}
void dot_boot_button(int down, unsigned now) {
  down = !!down;
  if (ui.boot_down == down)
    return;
  ui.boot_down = down;
  // 0.5.3: with the screen off, BOOT only wakes the lock screen; while locked
  // it does nothing else. The release of that press is swallowed, also when it
  // comes after unlocking.
  if (ui.locked || ui.screen == POWER_OFF) {
    if (down && ui.dark)
      wake_locked(now);
    ui.touched = now;
    ui.boot_swallow = down;
    return;
  }
  if (ui.boot_swallow) {
    ui.boot_swallow = 0;
    if (!down)
      return;
  }
  ui.touched = now;
  if (preferences.boot == SHORT_TALK) {
    if (down)
      begin_hardware_talk(now, 1);
    else if (ui.hardware_talk == 1)
      end_hardware_talk(now);
  } else if (!down)
    shortcut(preferences.boot, now);
}
void dot_power_key(int down, unsigned now) {
  down = !!down;
  if (ui.power_down == down)
    return;
  ui.power_down = down;
  ui.touched = now;
  if (down) {
    ui.power_at = now;
    return;
  }
  if (ui.power_off)
    return;
  if (ui.screen == POWER_OFF) {
    // Released before 3 s: back to where the hold started, dark if it was.
    ui.screen = ui.before_power;
    ui.started = ui.before_power_started;
    ui.dark = ui.power_was_dark;
    return;
  }
  if (now - ui.power_at >= POWER_SHOW_MS || ui.screen == LISTEN)
    return;
  if (ui.dark)
    wake_locked(now);
  else if (ui.locked)
    ui.dark = 1;
  else
    lock_screen(now);
}
void dot_power_button(unsigned now) {
  dot_power_key(1, now);
  dot_power_key(0, now);
}
static void set_slider(int x, int brightness) {
  int value = max(0, min(100, (x - 98) * 100 / 270));
  if (brightness)
    preferences.brightness = max(5, value);
  else {
    preferences.volume = value;
    preferences.muted = 0;
  }
}
void dot_down(int x, int y, unsigned now) {
  if (ui.dark || ui.screen == POWER_OFF)
    return;
  disarm();
  ui.down = 1;
  ui.press_at = now;
  ui.x0 = x;
  ui.y0 = y;
  ui.last = now;
  ui.touched = now;
  ui.cancelled = 0;
  if (ui.screen == SETTINGS && x >= 76 && x <= 390 && y >= 132 && y <= 253)
    set_slider(x, y >= 194);
  ui.hold_armed = (ui.screen == IDLE && y >= 90 && y <= 424);
  if (ui.hold_armed) {
    ui.preroll = 1;
    emit(MIC_ARM);
  }
}
void dot_move(int x, int y, unsigned now) {
  (void)now;
  if (!ui.down)
    return;
  if (ui.screen == LOCKED) {
    // The lock screen follows the finger up, in 6 px steps.
    ui.swipe = max(0, min(156, ui.y0 - y)) / 6 * 6;
    return;
  }
  if (ui.screen == SETTINGS && ui.x0 >= 76 && ui.x0 <= 390 && ui.y0 >= 132 &&
      ui.y0 <= 253) {
    if (x >= 0 && y >= 0)
      set_slider(x, ui.y0 >= 194);
  } else if (ui.screen == LISTEN && !ui.hardware_talk) {
    ui.cancelled = (y < ui.y0 - 65 || absi(x - ui.x0) > 95);
  } else if (absi(x - ui.x0) > 42 || absi(y - ui.y0) > 42) {
    ui.hold_armed = 0;
    disarm();
  }
}
void dot_tick(unsigned now) {
  if (ui.down && ui.hold_armed && ui.screen == IDLE &&
      now - ui.press_at >= 260) {
    ui.screen = LISTEN;
    ui.started = now;
    ui.hold_armed = 0;
    ui.level = 0;
    ui.preroll = 0;
    emit(MIC_START);
  }
  if (ui.screen == LISTEN &&
      now - ui.started >= DOT_MAX_RECORD_SECONDS * 1000u) {
    ui.hardware_talk = 0;
    if (ui.cancelled) {
      emit(MIC_DISCARD);
      change(home(), now);
    } else {
      emit(MIC_SEND);
      change(TRANSFER, now);
    }
  }
  if (ui.screen == TRANSFER && now - ui.started >= 150000)
    dot_error("Confira a entrega no celular.", now);
  if (ui.screen == WAITING && now - ui.started >= 90000)
    dot_error("O Dot ainda pode responder.", now);
  if (ui.screen == IDLE && !ui.down && now - ui.last >= DIM_MS)
    change(REST, now);
  // 0.5.3: the voice keeps the screen awake; PWR held shows the power ring.
  if (ui.playback_level > 0)
    ui.touched = now;
  if (ui.power_down && !ui.power_off && ui.screen != LISTEN) {
    unsigned held = now - ui.power_at;
    if (held >= POWER_SHOW_MS && ui.screen != POWER_OFF) {
      ui.before_power = ui.screen;
      ui.before_power_started = ui.started;
      ui.power_was_dark = ui.dark;
      disarm();
      change(POWER_OFF, now);
      ui.dark = 0;
    }
    if (held >= POWER_OFF_MS && ui.screen == POWER_OFF) {
      ui.power_off = 1;
      ui.started = now;
    }
  }
  if (ui.screen == LOCKED && !ui.dark && !ui.down &&
      now - ui.touched >= LOCK_LIT_MS)
    ui.dark = 1;
  if (!ui.locked && !ui.down && may_dark() && now - ui.touched >= DARK_MS)
    lock_screen(now);
}
void dot_up(int x, int y, unsigned now) {
  if (!ui.down)
    return;
  dot_move(x, y, now);
  dot_tick(now);
  if (!ui.down)
    return;
  int sx = ui.x0, sy = ui.y0;
  ui.down = 0;
  ui.hold_armed = 0;
  disarm();
  ui.last = now;
  ui.touched = now;
  if (ui.screen == LOCKED) {
    // Only a long swipe up from the lower part unlocks. Taps, holds and the
    // settings swipe from the top do nothing here.
    if (sy >= 200 && sy - y >= 110 && absi(x - sx) < 110)
      unlock(now);
    else
      ui.swipe = 0;
    return;
  }
  if (!is_menu() && ui.screen != LISTEN && sy <= 90 && y - sy >= 57 &&
      absi(sx - x) < 90) {
    dot_open_settings(now);
    return;
  }
  if (ui.screen == LISTEN && ui.hardware_talk)
    return;
  if (is_menu()) {
    if (absi(sx - x) > 36 || absi(sy - y) > 30 || x < 62 || x > 404)
      return;
    if (y >= 376 && y <= 424) {
      if (ui.screen == SETTINGS)
        close_settings(now);
      else
        change(ui.screen == PICK_BUTTON ? BUTTONS : SETTINGS, now);
    } else if (ui.screen == SETTINGS && y >= 260 && y < 313) {
      preferences.bluetooth = !preferences.bluetooth;
      if (!preferences.bluetooth) {
        ui.paired = 0;
        ui.online = 0;
        ui.key = -1;
      }
    } else if (ui.screen == SETTINGS && y >= 316 && y <= 368) {
      change(BUTTONS, now);
    } else if (ui.screen == BUTTONS && y >= 149 && y < 232) {
      // 0.5.3: only BOOT is configurable; PWR is the screen and power key.
      ui.editing_power = 0;
      change(PICK_BUTTON, now);
    } else if (ui.screen == PICK_BUTTON && y >= 128 && y < 363) {
      int selected = (y - 128) / 47;
      if (ui.editing_power)
        preferences.power = selected;
      else
        preferences.boot = selected;
      change(BUTTONS, now);
    }
    return;
  }
  if (ui.screen == LISTEN) {
    if (ui.cancelled || now - ui.started < 450) {
      emit(MIC_DISCARD);
      change(home(), now);
    } else {
      emit(MIC_SEND);
      change(TRANSFER, now);
    }
    return;
  }
  if (ui.screen == RADIO_OFF && y >= 335 && y <= 415) {
    dot_open_settings(now);
    return;
  }
  if (sy < 86 && sx >= 282 && sx <= 347 && y < 86 && absi(x - sx) < 30) {
    dot_open_settings(now);
    return;
  }
  if (ui.screen == READ) {
    if (x - sx > 45 || y - sy > 60 || (sy > 345 && sx < 202)) {
      if (ui.page > 0)
        ui.page--;
      else
        change(ui.choice ? DECIDE : ANSWER, now);
      return;
    }
    if (sy < 100) {
      change(ui.choice ? DECIDE : ANSWER, now);
      return;
    }
    if (ui.page + 1 < ui.pages)
      ui.page++;
    else if (ui.choice)
      change(DECIDE, now);
    else {
      if (ui.has_answer)
        emit(DISMISS);
      ui.has_answer = 0;
      change(home(), now);
    }
    return;
  }
  if (absi(sx - x) > 50 || absi(sy - y) > 50)
    return;
  if (ui.screen == REST) {
    change(home(), now);
    return;
  }
  if (ui.screen == LINK) {
    if (y > 340) {
      emit(CHECK_LINK);
    }
    change(home(), now);
    return;
  }
  if (ui.screen == FAILURE) {
    if (y > 330) {
      emit(CHECK_LINK);
      change(home(), now);
    }
    return;
  }
  if (ui.screen == PAIR || ui.screen == OFFLINE) {
    ui.before_link = ui.screen;
    change(LINK, now);
    return;
  }
  if (ui.screen == ANSWER) {
    ui.page = 0;
    change(READ, now);
    return;
  }
  if (ui.screen == DECIDE) {
    if (y >= 350 && y <= 420) {
      if (sy < 356 || sy > 404 || y < 356 || y > 404)
        return;
      int no = sx >= 94 && sx <= 225 && x >= 94 && x <= 225;
      int yes = sx >= 240 && sx <= 379 && x >= 240 && x <= 379;
      if (!no && !yes)
        return;
      emit(yes ? APPROVE : DECLINE);
      ui.has_answer = 0;
      change(home(), now);
    } else {
      ui.page = 0;
      change(READ, now);
    }
    return;
  }
  if (ui.screen == IDLE && y < 90) {
    ui.before_link = IDLE;
    change(LINK, now);
  }
}
static const char *shortcut_name(int key, int power) {
  switch (key) {
  case SHORT_TALK:
    return power ? "Gravar / enviar" : "Segurar para falar";
  case SHORT_SETTINGS:
    return "Abrir ajustes";
  case SHORT_MUTE:
    return "Silenciar / ativar som";
  case SHORT_REPEAT:
    return "Repetir resposta";
  default:
    return "Sem atalho";
  }
}
/* ---- Lume: drawing ------------------------------------------------------
 * Everything below only paints. Touch geometry lives in dot_down/dot_up above
 * and is unchanged, so every button is drawn inside its original hit area. */
static int wave(unsigned n) { return sine[n & 255]; }
static int ifloor(float v) {
  int i = (int)v;
  return i - (v < (float)i);
}
static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// Newton square root; avoids libm so the badge, the browser and tests match.
static float fsqrt(float v) {
  if (v <= 0)
    return 0;
  union {
    float f;
    uint32_t u;
  } b = {v};
  b.u = 0x1fbd1df5u + (b.u >> 1);
  float r = b.f;
  r = 0.5f * (r + v / r);
  r = 0.5f * (r + v / r);
  return r;
}
static int isqrt(int v) {
  int r = (int)fsqrt((float)v);
  while (r * r > v)
    r--;
  while ((r + 1) * (r + 1) <= v)
    r++;
  return r;
}
// Mix towards b by v/256.
static uint16_t mix8(uint16_t a, uint16_t b, int v) {
  if (v <= 0)
    return a;
  if (v >= 256)
    return b;
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int r = ar + ((((b >> 11) - ar) * v + 128) >> 8);
  int g = ag + (((((b >> 5) & 63) - ag) * v + 128) >> 8);
  int z = ab + ((((b & 31) - ab) * v + 128) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | z);
}
static void plot(int x, int y, uint16_t c, int v) {
  if (v <= 0 || x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1)
    return;
  uint16_t *p = fb + y * DOT_SIDE + x;
  *p = v >= 256 ? c : mix8(*p, c, v);
}
/* Backdrop: true black for the AMOLED plus the warm halo behind the Dot. One
 * pass over the frame; the halo uses a distance-squared table with a 2x2
 * ordered dither, so RGB565 shows no bands. */
typedef struct {
  int cx, cy, r, level; // level 0..256
  uint8_t tint[3];      // colour at the centre when level is 256
} Halo;
static uint16_t halo_lut[4][257];
static Halo halo_key;
static int halo_ready, corners_clean;
static void backdrop(const Halo *h) {
  int n = 0, k = 0, r2 = 0;
  if (h && h->level > 0 && h->r > 8) {
    r2 = h->r * h->r;
    while ((r2 >> k) > 256)
      k++;
    n = r2 >> k;
    // The table only depends on radius, level and tint: build it once per look.
    if (!halo_ready || h->r != halo_key.r || h->level != halo_key.level ||
        h->tint[0] != halo_key.tint[0] || h->tint[1] != halo_key.tint[1] ||
        h->tint[2] != halo_key.tint[2]) {
      static const int threshold[4] = {0, 2, 3, 1};
      for (int i = 0; i <= n; i++) {
        int v = h->level * halo_curve[i * 256 / n];
        int qr = h->tint[0] * 31 * v / (255 * 16320),
            qg = h->tint[1] * 63 * v / (255 * 16320),
            qb = h->tint[2] * 31 * v / (255 * 16320);
        for (int p = 0; p < 4; p++) {
          int t = threshold[p];
          halo_lut[p][i] = (uint16_t)(((qr + t) >> 2) << 11 | ((qg + t) >> 2) << 5 |
                                      ((qb + t) >> 2));
        }
      }
      halo_key = *h;
      halo_ready = 1;
    }
  }
  for (int y = clip_y0; y < clip_y1; y++) {
    uint16_t *row = fb + y * DOT_SIDE;
    int a = clip_x0, b = clip_x1;
    if (corners_clean) {
      // The glass is round: once the corners are black they never need paint.
      int half = isqrt(233 * 233 - (y - 233) * (y - 233)) + 2;
      a = max(a, 233 - half), b = min(b, 233 + half);
    }
    int x = a, dy = y - (n ? h->cy : 0);
    if (n && dy * dy < r2) {
      int half = isqrt(r2 - dy * dy);
      int x0 = max(a, h->cx - half), x1 = min(b, h->cx + half + 1);
      if (x0 < x1) {
        for (; x < x0; x++)
          row[x] = BG;
        const uint16_t *even = halo_lut[(y & 1) * 2], *odd = halo_lut[(y & 1) * 2 + 1];
        int dx = x0 - h->cx, d2 = dx * dx + dy * dy;
        for (; x < x1; x++, dx++) {
          int i = d2 >> k;
          if (i > n)
            i = n;
          row[x] = (x & 1 ? odd : even)[i];
          d2 += 2 * dx + 1;
        }
      }
    }
    for (; x < b; x++)
      row[x] = BG;
  }
}

// Anti-aliased filled circle; v is the opacity 0..256.
static void disc(float cx, float cy, float r, uint16_t c, int v) {
  int y0 = max(clip_y0, ifloor(cy - r - 1)), y1 = min(clip_y1, ifloor(cy + r + 2));
  if (cx + r + 2 < clip_x0 || cx - r - 2 >= clip_x1)
    return;
  float ro = r + 0.5f, ri = r - 0.5f;
  for (int y = y0; y < y1; y++) {
    float py = y + 0.5f - cy, qo = ro * ro - py * py;
    if (qo <= 0)
      continue;
    float ho = fsqrt(qo), qi = ri * ri - py * py, hi = (ri > 0 && qi > 0) ? fsqrt(qi) : -1;
    for (int x = ifloor(cx - ho); x <= ifloor(cx + ho); x++) {
      float px = x + 0.5f - cx;
      float cov = (hi >= 0 && px >= -hi && px <= hi)
                      ? 1
                      : clampf(r + 0.5f - fsqrt(px * px + py * py));
      plot(x, y, c, (int)(cov * v));
    }
  }
}
// Anti-aliased rounded rectangle; the top and bottom colours form a gradient.
static void rrect2(int x, int y, int w, int h, int r, uint16_t top,
                   uint16_t bottom, int v) {
  r = min(r, min(w, h) / 2);
  if (x >= clip_x1 || x + w <= clip_x0 || y >= clip_y1 || y + h <= clip_y0)
    return;
  for (int j = max(0, clip_y0 - y); j < min(h, clip_y1 - y); j++) {
    int yy = y + j;
    uint16_t c = top == bottom ? top : mix8(top, bottom, h > 1 ? j * 256 / (h - 1) : 0);
    float py = j < r ? r - j - 0.5f : j >= h - r ? j + 0.5f - (h - r) : 0;
    if (py <= 0) {
      int a = max(clip_x0, x), b = min(clip_x1, x + w);
      uint16_t *p = fb + yy * DOT_SIDE;
      for (int xx = a; xx < b; xx++)
        p[xx] = v >= 256 ? c : mix8(p[xx], c, v);
      continue;
    }
    float inner = (r - 0.5f) * (r - 0.5f), outer = (r + 0.5f) * (r + 0.5f);
    for (int i = 0; i < r; i++) {
      float px = r - i - 0.5f, d2 = px * px + py * py;
      float cov = d2 <= inner ? 1 : d2 >= outer ? 0 : clampf(r + 0.5f - fsqrt(d2));
      if (cov > 0) {
        plot(x + i, yy, c, (int)(cov * v));
        plot(x + w - 1 - i, yy, c, (int)(cov * v));
      }
    }
    int edge = (int)(clampf(r + 0.5f - py) * v);
    for (int xx = x + r; xx < x + w - r; xx++)
      plot(xx, yy, c, edge);
  }
}
static void rrect(int x, int y, int w, int h, int r, uint16_t c, int v) {
  rrect2(x, y, w, h, r, c, c, v);
}

/* Soft glow below a capsule, like light spilling from a lit key. Only the lower
 * half is painted; the key itself covers the rest. */
static void glow(int x0, int x1, int cy, int r, int spread, uint16_t c, int peak) {
  int r2 = r * r, R2 = (r + spread) * (r + spread);
  uint8_t fall[65];
  for (int i = 0; i <= 64; i++) {
    float d = fsqrt(r2 + (float)(R2 - r2) * i / 64) - r, t = 1 - d / spread;
    fall[i] = (uint8_t)(t <= 0 ? 0 : 255 * t * t);
  }
  int scale = (64 << 16) / (R2 - r2);
  for (int y = max(clip_y0, cy); y < min(clip_y1, cy + r + spread); y++) {
    int dy = y - cy;
    uint16_t *row = fb + y * DOT_SIDE;
    for (int x = max(clip_x0, x0 - r - spread); x < min(clip_x1, x1 + r + spread); x++) {
      int dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0, d2 = dx * dx + dy * dy;
      if (d2 >= R2)
        continue;
      int i = d2 <= r2 ? 0 : (int)(((long long)(d2 - r2) * scale) >> 16);
      row[x] = mix8(row[x], c, peak * fall[min(64, i)] >> 8);
    }
  }
}

/* The ring around the glass. Pseudo-angles run clockwise from the top in 1024
 * steps; they are monotonic, so arc limits computed the same way line up. */
static int pangle(float x, float y) {
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, s = ax + ay, p;
  if (s <= 0)
    return 0;
  if (x >= 0 && y < 0)
    p = ax / s;
  else if (x > 0 && y >= 0)
    p = 1 + ay / s;
  else if (x <= 0 && y > 0)
    p = 2 + ax / s;
  else
    p = 3 + ay / s;
  return (int)(p * 256) & 1023;
}
static int pangle_of(int index) { // index 0..255 around the circle, 0 at the top
  return pangle((float)wave(index), (float)-wave(index + 64));
}
enum { BAND_SOLID, BAND_ARC, BAND_COMET };
#define RING_R 221.0f
static void band(float hw, int mode, int from, int to, uint16_t c0, uint16_t c1,
                 int v, int ylo, int yhi) {
  // BAND_ARC lights `to` pseudo-angle steps clockwise from `from` (1024 = all);
  // BAND_COMET has its head at `from` and a tail of `to` steps. Rows outside
  // [ylo, yhi) are skipped. Distance to the ring uses (d^2 - R^2) / 2R, exact
  // to a few hundredths of a pixel inside the band and free of square roots.
  float ro = RING_R + hw + 0.5f, ri = RING_R - hw - 0.5f, r2 = RING_R * RING_R,
        inv2r = 0.5f / RING_R;
  int inv = to > 0 ? (256 << 16) / to : 0;
  for (int y = max(max(clip_y0, ylo), ifloor(233 - ro));
       y < min(min(clip_y1, yhi), ifloor(233 + ro) + 1); y++) {
    float py = y + 0.5f - 233, qo = ro * ro - py * py;
    if (qo <= 0)
      continue;
    float ho = fsqrt(qo), qi = ri * ri - py * py, hi = qi > 0 ? fsqrt(qi) : 0;
    uint16_t *row = fb + y * DOT_SIDE;
    for (int side = 0; side < 2; side++) {
      int a = max(clip_x0, side ? ifloor(233 + hi) : ifloor(233 - ho)),
          b = min(clip_x1 - 1, side ? ifloor(233 + ho) : ifloor(233 - hi));
      for (int x = a; x <= b; x++) {
        float px = x + 0.5f - 233, d = (px * px + py * py - r2) * inv2r;
        float cov = hw + 0.5f - (d < 0 ? -d : d);
        if (cov <= 0)
          continue;
        uint16_t c = c0;
        int strength = 256;
        if (mode == BAND_ARC) {
          int at = (pangle(px, py) - from) & 1023;
          if (to < 1024 && at >= to)
            continue;
          // The gradient follows the position on the ring, not the head, so the
          // lit part never changes colour as the arc grows.
          c = mix8(c0, c1, (from + at) >> 2);
        } else if (mode == BAND_COMET) {
          int behind = (from - pangle(px, py)) & 1023;
          if (behind >= to)
            continue;
          int t = 256 - ((behind * inv) >> 16);
          strength = t * t >> 8;
          c = mix8(c0, c1, t);
        }
        int alpha = (int)((cov > 1 ? 1 : cov) * v) * strength >> 8;
        if (alpha > 0)
          row[x] = alpha >= 256 ? c : mix8(row[x], c, alpha);
      }
    }
  }
}
static void ring_point(int index, float *x, float *y) {
  *x = 233 + RING_R * wave(index) / 256;
  *y = 233 - RING_R * wave(index + 64) / 256;
}
// Rows touched by the ring between two indices (0..256 around the circle).
static void arc_rows(int from, int to, int *ylo, int *yhi) {
  float x, y;
  *ylo = DOT_SIDE, *yhi = 0;
  for (int i = from;; i += 4) {
    if (i > to)
      i = to;
    ring_point(i, &x, &y);
    *ylo = min(*ylo, (int)y - 6), *yhi = max(*yhi, (int)y + 7);
    if (i == to)
      break;
  }
}
static void spark(float x, float y, uint16_t core, int glow_level) {
  // A bright head with a halo of its own: three discs of falling opacity.
  disc(x, y, 15, AMBER, glow_level / 7);
  disc(x, y, 10, AMBER, glow_level / 4);
  disc(x, y, 6.5f, core, 256);
}

/* Glyph bitmaps (text and icons) */
static void glyph_at(const Font *f, const Glyph *g, int x, int y, uint16_t col) {
  if (y >= clip_y1 || y + g->h <= clip_y0 || x >= clip_x1 || x + g->w <= clip_x0)
    return;
  for (int j = 0; j < g->h; j++) {
    int py = y + j;
    if (py < clip_y0 || py >= clip_y1)
      continue;
    for (int i = 0; i < g->w; i++) {
      int at = j * g->w + i, a = f->alpha[g->offset + at / 2];
      a = (at & 1) ? (a & 15) : (a >> 4);
      int px = x + i;
      if (a && px >= clip_x0 && px < clip_x1) {
        uint16_t *p = fb + py * DOT_SIDE + px;
        *p = a == 15 ? col : blend(*p, col, a);
      }
    }
  }
}
enum { IC_BT, IC_SLIDERS, IC_MIC, IC_BATTERY, IC_BOLT, IC_CHEVRON, IC_BOARD,
       IC_PHONE, IC_DOT, IC_CHECK };
static void icon(int i, int x, int y, uint16_t c) {
  glyph_at(&icons, icons.glyph + i, x, y, c);
}
static int icon_w(int i) { return icons.glyph[i].w; }
static void right(const Font *f, int xr, int base, const char *s, uint16_t c) {
  text_at(f, xr - text_width(f, s, 4000, 0), base, s, 4000, c, 0);
}
static int line_end_f(const Font *f, const char *s, int at, int w) {
  int i = at, space = -1, width = 0;
  for (; s[i] && s[i] != '\n';) {
    int n;
    int adv = f->glyph[glyph_i(cp(s + i, &n))].advance;
    if (width + adv > w && i > at)
      return space > at ? space : i;
    if (s[i] == ' ')
      space = i;
    width += adv;
    i += n;
  }
  return i;
}
static void wrap_center(const Font *f, const char *s, int baseline, int w, int lines,
                        int lead, uint16_t c) {
  int pos = skip(s, 0), dots = text_width(f, "...", 3, 0);
  for (int j = 0; j < lines && s[pos]; j++) {
    int end = line_end_f(f, s, pos, w), len = end - pos;
    int continued = j == lines - 1 && s[skip(s, end)];
    if (continued) {
      end = line_end_f(f, s, pos, w - dots);
      len = end - pos;
      while (len > 0 && s[pos + len - 1] == ' ')
        len--;
    }
    int tw = text_width(f, s + pos, len, 0), extra = continued ? dots : 0;
    int x = (DOT_SIDE - tw - extra) / 2;
    text_at(f, x, baseline + j * lead, s + pos, len, c, 0);
    if (continued)
      text_at(f, x + tw, baseline + j * lead, "...", 3, c, 0);
    pos = skip(s, end);
  }
}

/* Keys. Primary: lit amber with a soft spill below. Plate: warm dark with a
 * hairline edge. Labels use Inter 600. */
static void key(int x, int y, int w, int h, const char *label, int primary,
                int pressed, int with_mic) {
  int r = h / 2;
  if (primary) {
    if (!pressed)
      glow(x + r, x + w - r, y + h / 2 + 9, r - 6, 20, AMBER, 120);
    rrect2(x, y, w, h, r, pressed ? AMBER : AMBER_TOP, pressed ? AMBER_DEEP : AMBER_LOW,
           256);
    // Inner top light, as on a backlit key.
    rrect(x + r, y + 1, w - 2 * r, 1, 0, pressed ? AMBER : AMBER_SHINE, 140);
  } else {
    rrect(x, y, w, h, r, EDGE, 256);
    rrect(x + 1, y + 1, w - 2, h - 2, r - 1, pressed ? EDGE : PLATE, 256);
  }
  uint16_t fore = primary ? ON_AMBER : INK;
  int tw = text_width(&strong, label, 200, 0), gap = with_mic ? icon_w(IC_MIC) + 10 : 0;
  int tx = x + (w - tw - gap) / 2 + gap, base = y + h / 2 + 7;
  if (with_mic)
    icon(IC_MIC, tx - gap, y + (h - 22) / 2, fore);
  text_at(&strong, tx, base, label, 200, fore, 0);
}
static void key_center(int y, int w, int h, const char *label, int primary) {
  key((DOT_SIDE - w) / 2, y, w, h, label, primary, 0, 0);
}

/* The Dot. Poses are premultiplied RGB565 with a 4-bit mask: drawn 1:1 at the
 * main size and filtered bilinearly when smaller, always over the halo. The
 * filter works on RGB565 spread into 32 bits, so a pixel costs six multiplies. */
#define SPREAD_MASK 0x07E0F81Fu
static uint32_t spread(uint16_t c) { return (c | ((uint32_t)c << 16)) & SPREAD_MASK; }
static uint16_t unspread(uint32_t v) {
  v &= SPREAD_MASK;
  return (uint16_t)(v | (v >> 16));
}
static uint32_t lerp5(uint32_t a, uint32_t b, int f) { // f 0..32
  return ((a * (uint32_t)(32 - f) + b * (uint32_t)f) >> 5) & SPREAD_MASK;
}
static uint16_t greyed(uint16_t c) {
  int r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
  int y = (77 * r + 150 * g + 29 * b) >> 8;
  // 85 % grey and 75 % brightness: the Dot without colour while offline.
  r = (y * 218 + r * 38) * 3 >> 10;
  g = (y * 218 + g * 38) * 3 >> 10;
  b = (y * 218 + b * 38) * 3 >> 10;
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
static void over(uint16_t *p, uint16_t c, int a255) {
  // Premultiplied "over": colour + background * (1 - alpha).
  int k = 256 - (a255 + (a255 >> 7));
  uint16_t d = *p;
  int r = (c >> 11) + (((d >> 11) * k + 128) >> 8),
      g = ((c >> 5) & 63) + ((((d >> 5) & 63) * k + 128) >> 8),
      b = (c & 31) + (((d & 31) * k + 128) >> 8);
  *p = (uint16_t)(min(31, r) << 11 | min(63, g) << 5 | min(31, b));
}
static int nibble(const uint8_t *a, int i) {
  int v = a[i >> 1];
  return (i & 1) ? (v & 15) : (v >> 4);
}
static const Sprite *prescaled(int index, int scale) {
  int k = index <= 8 ? index : index >= 18 ? index - 9 : -1;
  if (k < 0)
    return 0;
  return scale == 200 ? poses_m + k : scale == 154 ? poses_s + k : 0;
}
static void blit(const Sprite *s, int left, int top, int grey) {
  int j1 = min(s->h, clip_y1 - top), i0 = max(0, clip_x0 - left), i1 = min(s->w, clip_x1 - left);
  for (int j = max(0, clip_y0 - top); j < j1; j++) {
    uint16_t *row = fb + (top + j) * DOT_SIDE + left;
    const uint16_t *src = s->pixels + j * s->w;
    int at = j * s->w;
    for (int i = i0; i < i1; i++) {
      int a = nibble(s->alpha, at + i);
      if (!a)
        continue;
      uint16_t c = grey ? greyed(src[i]) : src[i];
      if (a == 15)
        row[i] = c;
      else
        over(row + i, c, a * 17);
    }
  }
}
static void sprite_size(int index, int scale, int *w, int *h) {
  const Sprite *p = prescaled(index, scale);
  *w = p ? p->w : poses[index].w * scale / 256;
  *h = p ? p->h : poses[index].h * scale / 256;
}
static void sprite(int index, int cx, int cy, int scale, int grey) {
  const Sprite *p = prescaled(index, scale), *s = poses + index;
  if (p || scale == 256) {
    if (!p)
      p = s;
    blit(p, cx - p->w / 2, cy - p->h / 2, grey);
    return;
  }
  int w = s->w * scale / 256, h = s->h * scale / 256;
  int left = cx - w / 2, top = cy - h / 2;
  static int16_t col0[192];
  static uint8_t colf[192];
  int step_x = (s->w << 16) / w, step_y = (s->h << 16) / h;
  for (int i = 0; i < w; i++) {
    int sx = max(0, i * step_x + step_x / 2 - 32768);
    col0[i] = (int16_t)(sx >> 16), colf[i] = (uint8_t)((sx >> 11) & 31);
  }
  for (int j = max(0, clip_y0 - top); j < min(h, clip_y1 - top); j++) {
    int py = top + j;
    int sy = max(0, j * step_y + step_y / 2 - 32768);
    int y0 = sy >> 16, fy = (sy >> 11) & 31, y1 = min(s->h - 1, y0 + 1);
    const uint16_t *r0 = s->pixels + y0 * s->w, *r1 = s->pixels + y1 * s->w;
    int o0 = y0 * s->w, o1 = y1 * s->w;
    uint16_t *row = fb + py * DOT_SIDE;
    for (int i = max(0, clip_x0 - left); i < min(w, clip_x1 - left); i++) {
      int x0 = col0[i], x1 = min(s->w - 1, x0 + 1), fx = colf[i];
      int a00 = nibble(s->alpha, o0 + x0), a10 = nibble(s->alpha, o0 + x1),
          a01 = nibble(s->alpha, o1 + x0), a11 = nibble(s->alpha, o1 + x1);
      if (!(a00 | a10 | a01 | a11))
        continue;
      int a = ((a00 * (32 - fx) + a10 * fx) * (32 - fy) + (a01 * (32 - fx) + a11 * fx) * fy) *
                  17 >> 10;
      uint16_t c = unspread(lerp5(lerp5(spread(r0[x0]), spread(r0[x1]), fx),
                                  lerp5(spread(r1[x0]), spread(r1[x1]), fx), fy));
      if (grey)
        c = greyed(c);
      if (a >= 255)
        row[left + i] = c;
      else if (a > 0)
        over(row + left + i, c, a);
    }
  }
}
static void rest_drift(unsigned now, int *dx, int *dy) {
  // Keep the AMOLED from holding a still image: drift a few pixels a minute.
  static const int8_t drift[8][2] = {{0, 0}, {3, -2}, {-3, 2}, {2, 3},
                                     {-2, -3}, {4, 1}, {-4, -1}, {1, 4}};
  int k = (int)((now / 60000u) % 8u);
  *dx = drift[k][0], *dy = drift[k][1];
}
typedef struct {
  int on, pose, cx, cy, scale, grey;
} Mascot;
static Mascot mascot_for(unsigned now) {
  Mascot m = {1, -1, 233, 190, 256, 0};
  switch (ui.screen) {
  case PAIR:
    m.cy = 148, m.scale = 154;
    break;
  case IDLE:
    break;
  case LISTEN:
    m.cy = 170;
    break;
  case TRANSFER:
  case WAITING:
    m.cy = 182;
    break;
  case ANSWER:
    m.cy = 158, m.scale = 200;
    break;
  case DECIDE:
    m.cy = 138, m.scale = 154, m.pose = 1;
    break;
  case OFFLINE:
    m.cy = 172, m.scale = 200, m.grey = 1;
    break;
  case FAILURE:
    m.cy = 148, m.scale = 154, m.pose = 1;
    break;
  case RADIO_OFF:
    m.cy = 170, m.scale = 200, m.grey = 1;
    break;
  case REST: {
    int dx, dy;
    rest_drift(now, &dx, &dy);
    m.cx += dx, m.cy = 206 + dy, m.scale = 154, m.pose = 4;
    break;
  }
  case LOCKED:
    // Asleep while locked; awake when a reply is waiting behind the lock.
    m.cy = 182 - ui.swipe / 2, m.scale = 154, m.pose = ui.has_answer ? 0 : 4;
    break;
  case POWER_OFF:
    m.cy = 158, m.scale = 154, m.pose = 4, m.grey = ui.power_off;
    break;
  default:
    m.on = 0;
  }
  if (!m.on || m.pose >= 0)
    return m;
  // Actions share a fixed anchor. The eyelids close and reopen over nine
  // drawings; the mouth opens according to audio written to the speaker.
  unsigned t = now - ui.started, blink = now % 4150;
  if (ui.quiet)
    m.pose = 0;
  else if (ui.playback_level > 0)
    m.pose = 18 + min(8, 1 + ui.playback_level * 7 / 100);
  else if (blink < 435)
    m.pose = min(8, blink / 48);
  else if (ui.screen == LISTEN)
    m.pose = 9 + min(8, (int)(t / 80));
  else if (ui.screen == WAITING) {
    unsigned sequence = (t / 112) % 16;
    m.pose = 9 + (sequence > 8 ? 16 - sequence : sequence);
  } else
    m.pose = 0;
  _Static_assert(sizeof(poses) / sizeof(poses[0]) == 27,
                 "Amber action atlas must have 27 frames");
  return m;
}

/* Status: Bluetooth, reachability, battery, settings shortcut. */
static void battery_badge(int x, int y) {
  int charging = (battery_flags & 7) == 7;
  uint16_t color = charging ? MINT : battery_percent <= 15 ? CORAL : SOFT;
  icon(IC_BATTERY, x, y, color);
  if (battery_percent <= 100 && battery_percent > 0)
    rrect(x + 3, y + 3, max(2, battery_percent * 19 / 100), 8, 2, color, 256);
  char label[8] = "--";
  if (battery_percent <= 100) {
    num(label, battery_percent);
    int i = 0;
    while (label[i])
      i++;
    label[i++] = '%';
    label[i] = 0;
  } else if ((battery_flags & 9) == 9 && !(battery_flags & 2))
    copy_text(label, sizeof(label), "USB");
  int tx = x + 32;
  if (charging) {
    icon(IC_BOLT, tx, y, MINT);
    tx += 13;
  }
  text_at(&tiny, tx, y + 11, label, 8, SOFT, 0);
}
static void status_bar(void) {
  icon(IC_BT, 150, 53, ui.paired ? AMBER : FAINT);
  disc(169.5f, 63.5f, 3, ui.online ? MINT : FAINT, 256);
  battery_badge(184, 56);
  if (ui.screen != LISTEN && ui.screen != RADIO_OFF)
    icon(IC_SLIDERS, 302, 52, SOFT);
}
static void menu_header(void) {
  text_at(&micro, 150, 67, "MEU DOT", 16, AMBER, 2);
  battery_badge(250, 56);
}

static Halo halo_for(unsigned now) {
  Halo h = {233, 192, 190, 0, {100, 63, 14}};
  switch (ui.screen) {
  case IDLE:
    h.level = 230;
    break;
  case LISTEN:
    h.cy = 176, h.r = 205, h.level = 236;
    break;
  case WAITING:
  case TRANSFER:
    h.cy = 184, h.level = 200;
    break;
  case ANSWER:
    h.cy = 158, h.r = 175, h.level = 214;
    break;
  case DECIDE:
    h.cy = 140, h.r = 120, h.level = 180;
    break;
  case PAIR:
    h.cy = 150, h.r = 140, h.level = 190;
    break;
  case FAILURE:
    h.cy = 150, h.r = 130, h.level = 96;
    break;
  case OFFLINE:
  case RADIO_OFF:
    // A cool, colourless glow for a colourless Dot.
    h.cy = ui.screen == OFFLINE ? 176 : 172, h.r = 170, h.level = 150;
    h.tint[0] = 34, h.tint[1] = 32, h.tint[2] = 30;
    break;
  case REST: {
    int dx, dy;
    rest_drift(now, &dx, &dy);
    h.cx += dx, h.cy = 206 + dy, h.r = 120, h.level = 96;
    break;
  }
  case LOCKED:
    h.cy = 184 - ui.swipe / 2, h.r = 125, h.level = ui.has_answer ? 170 : 84;
    break;
  case POWER_OFF:
    h.cy = 160, h.r = 115, h.level = ui.power_off ? 0 : 64;
    break;
  default:
    h.level = 0;
  }
  return h;
}

static void bar_heights(unsigned now, int out[21]) {
  for (int i = 0; i < 21; i++) {
    int envelope = 11 - absi(i - 10),
        v = ui.quiet ? 128 : absi(wave(now / 4 + i * 27));
    out[i] = max(4, min(34, 4 + (ui.level + 10) * envelope * v / 8200));
  }
}
static void bars(unsigned now) {
  int h[21];
  bar_heights(now, h);
  for (int i = 0; i < 21; i++) {
    int fade = 256 - absi(i - 10) * 15;
    rrect(131 + i * 10, 368 - h[i] / 2, 4, h[i], 2, ui.cancelled ? CORAL : AMBER, fade);
  }
}
static void page_arc(void) {
  if (ui.pages < 2)
    return;
  // Track from 60 to 120 degrees on the right edge; the page is a lit slice.
  int a0 = pangle_of(43), lo, hi;
  arc_rows(43, 85, &lo, &hi);
  band(2.5f, BAND_ARC, a0, (pangle_of(85) - a0) & 1023, EDGE, EDGE, 256, lo, hi);
  int s0 = 43 + 42 * ui.page / ui.pages, s1 = 43 + 42 * (ui.page + 1) / ui.pages;
  band(2.5f, BAND_ARC, pangle_of(s0), (pangle_of(s1) - pangle_of(s0)) & 1023, AMBER, AMBER,
       256, lo, hi);
}
static int progress_index(unsigned now) {
  unsigned t = now - ui.started;
  if (t > DOT_MAX_RECORD_SECONDS * 1000u)
    t = DOT_MAX_RECORD_SECONDS * 1000u;
  return (int)(t * 256 / (DOT_MAX_RECORD_SECONDS * 1000u));
}
// The PWR ring: empty until 0.6 s of hold, full at 3 s.
static int power_index(unsigned now) {
  if (ui.power_off)
    return 256;
  unsigned held = now - ui.power_at;
  if (!ui.power_down || held < POWER_SHOW_MS)
    return 0;
  return (int)min(256, (int)((held - POWER_SHOW_MS) * 256 / (POWER_OFF_MS - POWER_SHOW_MS)));
}
static int comet_index(unsigned now) {
  unsigned period = ui.screen == TRANSFER ? 900u : 1700u;
  return ui.quiet ? 40 : (int)((now % period) * 256 / period);
}
static void instruments(unsigned now) {
  if (ui.screen == LISTEN) {
    int index = progress_index(now);
    uint16_t c0 = ui.cancelled ? CORAL_DEEP : AMBER_DEEP, c1 = ui.cancelled ? CORAL : AMBER;
    int lo, hi;
    arc_rows(0, index, &lo, &hi);
    if (index > 0)
      band(2.5f, BAND_ARC, 0, index >= 256 ? 1024 : pangle_of(index), c0, c1, 256, lo, hi);
    float x, y;
    ring_point(index, &x, &y);
    spark(x, y, ui.cancelled ? CORAL : AMBER_HEAD, 200);
  } else if (ui.screen == WAITING || ui.screen == TRANSFER) {
    int fast = ui.screen == TRANSFER, index = comet_index(now), lo, hi;
    arc_rows(index - 63, index, &lo, &hi);
    band(2.5f, BAND_COMET, pangle_of(index), 250, AMBER_DEEP, fast ? AMBER_SHINE : AMBER_HEAD,
         256, lo, hi);
  } else if (ui.screen == READ)
    page_arc();
  else if (ui.screen == POWER_OFF) {
    int index = power_index(now), lo, hi;
    if (index > 0) {
      arc_rows(0, index, &lo, &hi);
      band(2.5f, BAND_ARC, 0, index >= 256 ? 1024 : pangle_of(index), CORAL_DEEP, CORAL, 256,
           lo, hi);
    }
    if (index > 0 && index < 256) {
      float x, y;
      ring_point(index, &x, &y);
      spark(x, y, CORAL, 200);
    }
  }
}

static void settings_row(int y, const char *label, const char *value) {
  text_at(&small, 90, y, label, 64, INK, 0);
  right(&small, 373, y, value, AMBER);
}
static void slider(int y, const char *label, int value, int muted) {
  char number[8];
  num(number, value);
  int n = 0;
  while (number[n])
    n++;
  number[n++] = '%';
  number[n] = 0;
  settings_row(y, label, muted ? "Mudo" : number);
  int track = y + 12, at = 98 + value * 270 / 100;
  rrect(96, track, 274, 6, 3, EDGE, 256);
  if (at - 96 >= 6)
    rrect2(96, track, at - 96, 6, 3, muted ? FAINT : AMBER_DEEP, muted ? FAINT : AMBER, 256);
  disc(at, track + 3.5f, 14, AMBER, muted ? 0 : 46);
  disc(at, track + 4.5f, 10.5f, BG, 150);
  disc(at, track + 3, 10, INK, 256);
}
static void toggle(int x, int y, int on) {
  rrect(x, y, 44, 26, 13, on ? AMBER : EDGE, 256);
  disc(on ? x + 31 : x + 13, y + 13, 10, on ? ON_AMBER : SOFT, 256);
}
static void back_key(void) { key(164, 378, 138, 42, "Voltar", 0, 0, 0); }
static void paint_settings(void) {
  const int power = ui.editing_power;
  menu_header();
  center(&headline, 113,
         ui.screen == SETTINGS  ? "Ajustes"
         : ui.screen == BUTTONS ? "Botões"
                                : (power ? "Botão PWR" : "Botão BOOT"),
         INK, 0);
  if (ui.screen == SETTINGS) {
    slider(156, "Volume", preferences.volume, preferences.muted);
    slider(218, "Brilho", preferences.brightness, 0);
    text_at(&small, 90, 295, "Bluetooth", 64, INK, 0);
    toggle(329, 276, preferences.bluetooth);
    span(90, 313, 283, EDGE);
    text_at(&small, 90, 350, "Atalhos dos botões", 64, INK, 0);
    icon(IC_CHEVRON, 364, 332, SOFT);
  } else if (ui.screen == BUTTONS) {
    static const int tops[2] = {144, 234};
    for (int i = 0; i < 2; i++) {
      int top = tops[i];
      rrect(82, top, 302, 80, 22, EDGE, 256);
      rrect(83, top + 1, 300, 78, 21, PLATE, 256);
      // 0.5.3: PWR is fixed, like a phone's power key.
      text_at(&micro, 104, top + 30, i ? "PWR  ·  TOQUE CURTO" : "BOOT", 30, AMBER, 2);
      text_at(&small, 104, top + 58, i ? "Apagar a tela" : shortcut_name(preferences.boot, 0), 52,
              i ? SOFT : INK, 0);
      if (!i)
        icon(IC_CHEVRON, 352, top + 33, SOFT);
    }
    center(&micro, 352, "SEGURE O PWR 3 S PARA DESLIGAR", FAINT, 2);
  } else {
    int selected = power ? preferences.power : preferences.boot;
    for (int i = 0; i < SHORT_COUNT; i++) {
      int top = 128 + 47 * i, on = i == selected;
      uint16_t ground = BG;
      if (on) {
        rrect(82, top, 302, 42, 21, EDGE, 256);
        rrect(83, top + 1, 300, 40, 20, PLATE, 256);
        ground = PLATE;
      }
      disc(105, top + 21, 8, on ? AMBER : FAINT, 256);
      disc(105, top + 21, 6, ground, 256);
      if (on)
        disc(105, top + 21, 3.5f, AMBER, 256);
      text_at(&small, 126, top + 28, shortcut_name(i, power), 64, on ? INK : SOFT, 0);
    }
  }
  back_key();
}

static void paint_link(void) {
  center(&headline, 121, "Conexão", INK, 0);
  static const int xs[3] = {106, 206, 306};
  int lit[3] = {1, ui.paired, ui.online};
  for (int i = 0; i < 2; i++) {
    int on = lit[i] && lit[i + 1], x0 = xs[i] + 54, x1 = xs[i + 1];
    if (on)
      rrect(x0, 176, x1 - x0, 2, 1, AMBER, 256);
    else
      for (int x = x0 + 3; x < x1 - 3; x += 9)
        rrect(x, 176, 5, 2, 1, FAINT, 256);
  }
  static const int glyphs[3] = {IC_BOARD, IC_PHONE, IC_DOT};
  static const char *names[3] = {"PLACA", "CELULAR", "DOT"};
  for (int i = 0; i < 3; i++) {
    float cx = xs[i] + 27, cy = 177;
    if (lit[i])
      disc(cx, cy, 33, AMBER, 34);
    disc(cx, cy, 27, lit[i] ? AMBER : EDGE, 256);
    disc(cx, cy, 25.5f, PLATE, 256);
    const Glyph *g = icons.glyph + glyphs[i];
    icon(glyphs[i], (int)cx - g->w / 2, (int)cy - g->h / 2, lit[i] ? INK : FAINT);
    if (i == 0)
      disc(cx, cy, 3, AMBER, 256);
    int tw = text_width(&micro, names[i], 16, 1);
    text_at(&micro, (int)cx - tw / 2, 230, names[i], 16, FAINT, 1);
  }
  text_at(&small, 90, 272, "Placa e celular", 64, INK, 0);
  right(&small, 373, 272, ui.paired ? "Pareados" : "Aguardando", ui.paired ? MINT : AMBER);
  span(90, 287, 283, EDGE);
  text_at(&small, 90, 318, "Celular e Dot", 64, INK, 0);
  right(&small, 373, 318, ui.online ? "Disponível" : "Sem conexão", ui.online ? MINT : CORAL);
  key_center(352, 160, 50, "Verificar", 0);
}

static void paint_all(unsigned now) {
  Halo halo = halo_for(now);
  backdrop(&halo);
  if (ui.screen != REST) {
    band(1.0f, BAND_SOLID, 0, 0, RING, RING, 256, 0, DOT_SIDE);
    instruments(now);
    if (!is_menu() && ui.screen != LOCKED && ui.screen != POWER_OFF)
      status_bar();
  }
  Mascot m = mascot_for(now);
  if (m.on)
    sprite(m.pose, m.cx, m.cy, m.scale, m.grey);
  char digits[16];
  switch (ui.screen) {
  case PAIR:
    if (ui.key >= 0) {
      char key_text[8];
      int n = ui.key;
      for (int i = 6; i >= 0; i--) {
        if (i == 3) {
          key_text[i] = ' ';
          continue;
        }
        key_text[i] = (char)('0' + n % 10);
        n /= 10;
      }
      key_text[7] = 0;
      center(&micro, 245, "CÓDIGO BLUETOOTH", AMBER, 2);
      center(&display, 305, key_text, INK, 2);
      center(&small, 340, "Confirme o mesmo número no celular", SOFT, 0);
    } else {
      center(&headline, 279, "Vamos parear", INK, 0);
      center(&small, 316, "Abra o Meu Dot no celular.", SOFT, 0);
    }
    key_center(356, 160, 50, "Ver conexão", 0);
    break;
  case IDLE:
    center(&small, 316, "Toque e segure em qualquer lugar", SOFT, 0);
    key(103, 342, 260, 56, "Segure para falar", 1, ui.down, 1);
    break;
  case LISTEN: {
    center(&headline, 291, ui.cancelled ? "Cancelar?" : "Ouvindo", INK, 0);
    int s = (now - ui.started) / 1000;
    char timer[6] = {'0', ':', (char)('0' + s / 10), (char)('0' + s % 10), 0};
    center(&strong, 323, timer, ui.cancelled ? CORAL : AMBER, 1);
    bars(now);
    center(&micro, 415,
           ui.cancelled            ? "SOLTE PARA CANCELAR"
           : ui.hardware_talk == 2 ? "APERTE PWR PARA ENVIAR"
                                   : "SOLTE PARA ENVIAR",
           SOFT, 2);
    break;
  }
  case TRANSFER:
    center(&headline, 323, "Enviando", INK, 0);
    center(&small, 356, "Sua voz vai para o celular", SOFT, 0);
    break;
  case WAITING:
    center(&headline, 323, "Pensando", INK, 0);
    center(&small, 356, "O Dot está preparando a resposta", SOFT, 0);
    break;
  case ANSWER:
    center(&headline, 283, "Dot respondeu", INK, 0);
    wrap_center(&small, ui.reply, 316, 300, 1, 24, SOFT);
    key(133, 344, 200, 54, "Ler resposta", 1, 0, 0);
    break;
  case READ: {
    center(&micro, 111, ui.choice ? "PEDIDO DO DOT" : "RESPOSTA DO DOT", AMBER, 2);
    int pos = ui.offsets[ui.page];
    for (int j = 0; j < 5 && ui.reply[pos]; j++) {
      int end = line_end(ui.reply, pos, 292);
      text_at(&body, 87, 166 + j * 34, ui.reply + pos, end - pos, INK, 0);
      pos = skip(ui.reply, end);
    }
    char a[12], b[12];
    num(a, ui.page + 1);
    num(b, ui.pages);
    int k = 0;
    for (int j = 0; a[j]; j++)
      digits[k++] = a[j];
    digits[k++] = ' ';
    digits[k++] = '/';
    digits[k++] = ' ';
    for (int j = 0; b[j]; j++)
      digits[k++] = b[j];
    digits[k] = 0;
    center(&tiny, 340, digits, SOFT, 1);
    key(100, 356, 96, 50, "Voltar", 0, 0, 0);
    const char *next = ui.page + 1 < ui.pages ? "Avançar"
                       : ui.choice            ? "Decidir"
                                              : "Concluir";
    key(206, 356, 160, 50, next, 1, 0, 0);
    break;
  }
  case DECIDE:
    center(&headline, 241, "Sua decisão", INK, 0);
    wrap_center(&body, ui.reply, 281, 313, 2, 34, SOFT);
    key(94, 352, 131, 54, "Recusar", 0, 0, 0);
    key(240, 352, 139, 54, "Confirmar", 1, 0, 0);
    break;
  case OFFLINE:
    center(&headline, 297, "Dot offline", INK, 0);
    center(&small, 330, "Confira a conexão no celular.", SOFT, 0);
    key_center(352, 160, 50, "Ver conexão", 0);
    break;
  case FAILURE:
    center(&micro, 237, "PAUSA", CORAL, 2);
    center(&headline, 279, "Houve uma pausa", INK, 0);
    wrap_center(&small, ui.problem, 314, 313, 2, 24, SOFT);
    key_center(356, 160, 50, "Voltar", 0);
    break;
  case LINK:
    paint_link();
    break;
  case SETTINGS:
  case BUTTONS:
  case PICK_BUTTON:
    paint_settings();
    break;
  case RADIO_OFF:
    center(&headline, 293, "Bluetooth desligado", INK, 0);
    center(&small, 326, "Ative nos ajustes da placa.", SOFT, 0);
    key_center(352, 180, 50, "Abrir ajustes", 0);
    break;
  case REST:
    {
      int tw = text_width(&small, "Toque para acordar", 64, 0);
      text_at(&small, (DOT_SIDE - tw) / 2 + m.cx - 233, 316 + m.cy - 206, "Toque para acordar",
              64, FAINT, 0);
    }
    break;
  case LOCKED: {
    int lift = ui.swipe / 2;
    battery_badge(207, 56);
    if (ui.has_answer) {
      center(&micro, 296 - lift, ui.choice ? "O DOT PEDIU SUA DECISÃO" : "NOVA RESPOSTA", AMBER,
             2);
      center(&small, 326 - lift, "Desbloqueie para ver", SOFT, 0);
    } else
      center(&small, 312 - lift, "Bloqueado", SOFT, 0);
    // The handle rises with the finger and turns amber while dragged.
    center(&micro, 384 - ui.swipe, "DESLIZE PARA CIMA", FAINT, 2);
    rrect(208, 400 - ui.swipe, 50, 5, 3, ui.swipe ? AMBER : SOFT, 256);
    break;
  }
  case POWER_OFF:
    center(&headline, 274, ui.power_off ? "Até logo" : "Desligar", INK, 0);
    // The AXP2101 turns on after 0.5 s of PWR (REG 27h), so a tap is not enough.
    center(&small, 308, ui.power_off ? "Segure o PWR para ligar" : "Continue segurando o PWR",
           SOFT, 0);
    break;
  default:
    break;
  }
}

typedef struct {
  int x0, y0, x1, y1;
} Rect;
static Rect dirty[16];
static int dirty_count;
// Everything a frame shows apart from the moving parts below. Any change here
// repaints the whole screen.
#define SCENE_WORDS 20
static void scene_of(unsigned now, int *k) {
  int dx, dy;
  rest_drift(now, &dx, &dy);
  int i = 0;
  k[i++] = ui.screen, k[i++] = ui.paired, k[i++] = ui.online, k[i++] = (int)battery_flags;
  k[i++] = (int)battery_percent, k[i++] = ui.key, k[i++] = ui.page, k[i++] = ui.pages;
  k[i++] = ui.choice, k[i++] = ui.cancelled, k[i++] = ui.hardware_talk;
  k[i++] = ui.screen == IDLE && ui.down, k[i++] = ui.editing_power, k[i++] = ui.quiet;
  k[i++] = (int)ui.started, k[i++] = ui.screen == REST ? dx * 16 + dy : 0;
  k[i++] = preferences.volume | preferences.brightness << 8 | preferences.bluetooth << 16 |
           preferences.muted << 24;
  k[i++] = preferences.boot | preferences.power << 8;
  k[i++] = ui.has_answer;
  k[i++] = ui.swipe | ui.power_off << 9 | ui.dark << 10;
}
static unsigned full_paints;
static struct {
  int valid, scene[SCENE_WORDS];
  Mascot mascot;
  int seconds, bars[21], progress, comet;
} shown;
static void paint_reset(void) {
  shown.valid = 0;
  dirty_count = 0;
  corners_clean = 0;
  halo_ready = 0;
}
static Rect mascot_rect(const Mascot *m) {
  int w, h;
  sprite_size(m->pose, m->scale, &w, &h);
  Rect r = {m->cx - w / 2 - 1, m->cy - h / 2 - 1, m->cx - w / 2 + w + 1, m->cy - h / 2 + h + 1};
  return r;
}
static Rect ring_rect(int from, int to, int margin) {
  Rect r = {DOT_SIDE, DOT_SIDE, 0, 0};
  float x, y;
  for (int i = from;; i += 2) {
    if (i > to)
      i = to;
    ring_point(i, &x, &y);
    r.x0 = min(r.x0, (int)x - margin), r.y0 = min(r.y0, (int)y - margin);
    r.x1 = max(r.x1, (int)x + margin + 1), r.y1 = max(r.y1, (int)y + margin + 1);
    if (i == to)
      break;
  }
  return r;
}
static Rect join(Rect a, Rect b) {
  Rect r = {min(a.x0, b.x0), min(a.y0, b.y0), max(a.x1, b.x1), max(a.y1, b.y1)};
  return r;
}
static void mark(Rect r) {
  if (dirty_count < 16)
    dirty[dirty_count++] = r;
  else
    dirty[15] = join(dirty[15], r);
}
int dot_dirty(int index, int *x0, int *y0, int *x1, int *y1) {
  if (index < 0 || index >= dirty_count)
    return 0;
  *x0 = dirty[index].x0, *y0 = dirty[index].y0, *x1 = dirty[index].x1, *y1 = dirty[index].y1;
  return 1;
}
static void repaint(unsigned now, Rect r) {
  clip_x0 = max(0, r.x0), clip_y0 = max(0, r.y0);
  clip_x1 = min(DOT_SIDE, r.x1), clip_y1 = min(DOT_SIDE, r.y1);
  if (clip_x0 < clip_x1 && clip_y0 < clip_y1) {
    paint_all(now);
    Rect done = {clip_x0, clip_y0, clip_x1, clip_y1};
    mark(done);
  }
  clip_x0 = clip_y0 = 0, clip_x1 = clip_y1 = DOT_SIDE;
}
/* Writing the whole frame to PSRAM costs about 13 ms on the badge, so a frame
 * repaints only what moved: the Dot, the voice bars, the timer, the head of the
 * recording arc or the comet. A still screen costs nothing. */
static void paint_frame(unsigned now) {
  int scene[SCENE_WORDS], bars_now[21];
  scene_of(now, scene);
  Mascot m = mascot_for(now);
  int seconds = ui.screen == LISTEN ? (int)((now - ui.started) / 1000) : 0;
  int progress = ui.screen == LISTEN      ? progress_index(now)
                 : ui.screen == POWER_OFF ? power_index(now)
                                          : 0;
  int comet = ui.screen == WAITING || ui.screen == TRANSFER ? comet_index(now) : 0;
  if (ui.screen == LISTEN)
    bar_heights(now, bars_now);
  else
    for (int i = 0; i < 21; i++)
      bars_now[i] = 0;
  int same = shown.valid;
  for (int i = 0; same && i < SCENE_WORDS; i++)
    same = scene[i] == shown.scene[i];
  int step = (progress - shown.progress) & 255, turn = (comet - shown.comet) & 255;
  dirty_count = 0;
  // The PWR ring fills in 2.4 s, several steps a frame: repaint the arc slice.
  int fast_ring = ui.screen == POWER_OFF && step <= 24;
  if (!same || (step > 4 && !fast_ring) || turn > 40) {
    paint_all(now);
    Rect all = {0, 0, DOT_SIDE, DOT_SIDE};
    mark(all);
    corners_clean = 1;
    full_paints++;
  } else {
    if (m.on != shown.mascot.on || m.pose != shown.mascot.pose || m.cx != shown.mascot.cx ||
        m.cy != shown.mascot.cy || m.scale != shown.mascot.scale || m.grey != shown.mascot.grey) {
      Rect r = m.on ? mascot_rect(&m) : mascot_rect(&shown.mascot);
      if (m.on && shown.mascot.on)
        r = join(r, mascot_rect(&shown.mascot));
      repaint(now, r);
    }
    int bars_moved = 0;
    for (int i = 0; i < 21; i++)
      bars_moved |= bars_now[i] != shown.bars[i];
    if (bars_moved) {
      Rect r = {127, 349, 339, 388};
      repaint(now, r);
    }
    if (seconds != shown.seconds) {
      Rect r = {186, 300, 280, 330};
      repaint(now, r);
    }
    if (step && fast_ring)
      repaint(now, ring_rect(shown.progress, progress, 18));
    else if (step)
      repaint(now, join(ring_rect(shown.progress, shown.progress, 18), ring_rect(progress, progress, 18)));
    if (turn) {
      // The comet moved: repaint the ring from the old tail to the new head in
      // small pieces, so the halo underneath is rewritten only where needed.
      int from = shown.comet - 64, to = shown.comet + turn;
      for (int i = from; i < to; i += 8)
        repaint(now, ring_rect(i, min(to, i + 8), 6));
    }
  }
  shown.valid = 1;
  for (int i = 0; i < SCENE_WORDS; i++)
    shown.scene[i] = scene[i];
  for (int i = 0; i < 21; i++)
    shown.bars[i] = bars_now[i];
  shown.mascot = m, shown.seconds = seconds, shown.progress = progress, shown.comet = comet;
}

void dot_paint(unsigned now) {
#ifdef ESP_PLATFORM
  // Paint cost on the badge, logged every 512 frames (about 25 s at 20 fps).
  static uint32_t frames, total_us, worst_us;
  static int worst_screen;
  int64_t start = esp_timer_get_time();
  paint_frame(now);
  uint32_t spent = (uint32_t)(esp_timer_get_time() - start);
  total_us += spent;
  if (spent > worst_us)
    worst_us = spent, worst_screen = ui.screen;
  if (++frames == 512) {
    ESP_LOGI("dot_ui", "pintura_media_us=%lu pintura_max_us=%lu tela_max=%d completas=%u/512",
             (unsigned long)(total_us / frames), (unsigned long)worst_us, worst_screen, full_paints);
    frames = total_us = worst_us = 0;
    full_paints = 0;
  }
#else
  paint_frame(now);
#endif
}
#ifdef DOT_PREVIEW
static uint16_t screenbuf[DOT_SIDE * DOT_SIDE];
static char preview_text[1100];
uint16_t *preview_init(void) {
  dot_init(screenbuf);
  return screenbuf;
}
char *preview_input(void) { return preview_text; }
void preview_reply(int choice, unsigned now) {
  dot_answer(preview_text, choice, now);
}
void preview_error(unsigned now) { dot_error(preview_text, now); }
void preview_idle(unsigned now) {
  ui.has_answer = 0;
  change(home(), now);
}
void preview_rest(unsigned now) { change(REST, now); }
// Paints the current state from scratch, to check that partial repaints match.
void preview_repaint_all(unsigned now) {
  paint_reset();
  dot_paint(now);
}
#endif
