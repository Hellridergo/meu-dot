#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "device.h"
#include "dot_ui.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>

static unsigned now(void) { return esp_timer_get_time() / 1000; }
static atomic_int peak, speech_peak;
static int current_brightness = -1;
void device_speech_level(int level) { atomic_store(&speech_peak, level); }
static atomic_uint visible_tag;
uint32_t device_card_tag(void) { return atomic_load(&visible_tag); }
void device_battery(uint8_t flags, uint8_t percent) {
  if (bsp_display_lock(30) == ESP_OK) {
    dot_battery(flags, percent);
    bsp_display_unlock();
  }
}
static lv_obj_t *canvas;
static unsigned last_paint;
static int last_screen = -1;
void device_status(bool paired, bool online) {
  if (bsp_display_lock(3000) == ESP_OK) {
    dot_link(paired, online, now());
    bsp_display_unlock();
  }
}
void device_key(int code) {
  if (bsp_display_lock(3000) == ESP_OK) {
    dot_passkey(code);
    bsp_display_unlock();
  }
}
bool device_card(const char *s, int type, uint32_t tag) {
  if (bsp_display_lock(3000) == ESP_OK) {
    visible_tag = tag;
    if (type == 2)
      dot_error(s, now());
    else
      dot_answer(s, type, now());
    bsp_display_unlock();
    return true;
  }
  return false;
}
void device_reply(const char *s, int type) { device_card(s, type, 0); }
void device_delivered(void) {
  if (bsp_display_lock(3000) == ESP_OK) {
    dot_delivered(now());
    bsp_display_unlock();
  }
}
void device_level(int level) { atomic_store(&peak, level); }
static void touch(lv_event_t *e) {
  lv_indev_t *input = lv_indev_active();
  if (!input)
    return;
  lv_point_t p;
  lv_indev_get_point(input, &p);
  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    dot_down(p.x, p.y, now());
    break;
  case LV_EVENT_PRESSING:
    dot_move(p.x, p.y, now());
    break;
  case LV_EVENT_RELEASED:
    dot_up(p.x, p.y, now());
    break;
  case LV_EVENT_PRESS_LOST:
    dot_move(-500, -500, now());
    dot_up(-500, -500, now());
    break;
  default:
    break;
  }
}
// 0.5.3: PWR held for 3 s. Settings are saved, the panel goes dark and the PMU
// turns everything off; PWR turns the board back on. If the PMU does not answer
// (or the board is still alive a second later), sleep until PWR is pressed.
static void power_off_now(void) {
  bsp_display_brightness_set(0);
  controls_flush();
  ESP_LOGI("dot_power", "desligando pela PMU");
  if (battery_power_off())
    vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGW("dot_power", "ainda ligada; dormindo até o PWR");
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_3, 1); // Key2/PWRON reads high when pressed
  esp_deep_sleep_start();
}
static void update(lv_timer_t *t) {
  (void)t;
  unsigned ms = now();
  controls_tick(ms);
  dot_tick(ms);
  DotSettings settings = dot_settings();
  voice_set_volume(settings.muted ? 0 : settings.volume);
  dot_playback_level(atomic_load(&speech_peak));
  dot_sound_level(atomic_load(&peak));
  int action;
  while ((action = dot_next_action())) {
    if ((action >= MIC_START && action <= MIC_DISCARD) || action == PLAY_LAST ||
        action == MIC_ARM || action == MIC_DISARM)
      voice_action(action);
    else
      handset_action(action, visible_tag);
  }
  int screen = dot_screen_id();
  if (dot_power_off_requested()) {
    // The goodbye frame stays up for 0.9 s, then the PMU cuts the power.
    static unsigned asked;
    if (!asked)
      asked = ms | 1;
    else if (ms - asked >= 900)
      power_off_now();
  }
  // 0.5.3: dark screen. Nothing is painted and the panel stays at 0. On waking,
  // the new frame is painted first and the light comes on 120 ms later, so the
  // frame from before the dark never flashes.
  static int was_dark;
  static unsigned lit_at;
  if (!dot_display_on()) {
    if (current_brightness != 0 && bsp_display_brightness_set(0) == ESP_OK)
      current_brightness = 0;
    was_dark = 1;
    last_screen = -1;
    return;
  }
  if (was_dark) {
    was_dark = 0;
    lit_at = ms;
  }
  int brightness = settings.brightness;
  if (screen == REST && brightness > 12)
    brightness = 12;
  if (screen == LOCKED && brightness > 40)
    brightness = 40;
  if (ms - lit_at < 120)
    brightness = 0;
  if (brightness != current_brightness &&
      bsp_display_brightness_set(brightness) == ESP_OK)
    current_brightness = brightness;
  if (last_screen == screen && ms - last_paint < (screen == REST ? 460 : 48))
    return;
  dot_paint(ms);
  // Send the panel only what changed. A full frame is 434 KB over QSPI and
  // keeps the LVGL task busy for about 65 ms; a still frame sends nothing.
  int x0, y0, x1, y1;
  for (int i = 0; dot_dirty(i, &x0, &y0, &x1, &y1); i++) {
    lv_area_t area = {x0, y0, x1 - 1, y1 - 1};
    lv_obj_invalidate_area(canvas, &area);
  }
  last_paint = ms;
  last_screen = screen;
}
void app_main(void) {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);
  lv_display_t *display = bsp_display_start();
  if (!display)
    abort();
  uint16_t *pixels = heap_caps_aligned_alloc(
      64, 466 * 466 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!pixels)
    abort();
  ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
  controls_init();
  dot_init(pixels);
  dot_paint(now());
  lv_obj_t *root = lv_screen_active();
  lv_obj_remove_style_all(root);
  canvas = lv_canvas_create(root);
  lv_canvas_set_buffer(canvas, pixels, 466, 466, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(canvas, touch, LV_EVENT_ALL, NULL);
  bsp_display_backlight_on();
  lv_timer_create(update, 20, NULL);
  bsp_display_unlock();
  voice_init();
  handset_init(dot_settings().bluetooth);
  battery_init();
}
