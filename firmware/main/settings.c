#include "device.h"
#include "dot_ui.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

// On the 1.75C the Key2/PWRON signal reaches GPIO3 through an inverting FET.
// Sample both strapping pins only after boot, without changing board pulls.
typedef struct {
  gpio_num_t pin;
  bool high_on_press, raw, stable, armed;
  unsigned changed, pressed;
} Key;
static Key keys[] = {{.pin = GPIO_NUM_0},
                     {.pin = GPIO_NUM_3, .high_on_press = true}};
static nvs_handle_t store;
static DotSettings applied;
static bool dirty;
static unsigned modified;
static bool level(const Key *k) {
  return !!gpio_get_level(k->pin) == k->high_on_press;
}
void controls_init(void) {
  DotSettings saved;
  size_t len = sizeof(saved);
  if (nvs_open("dot_ui", NVS_READWRITE, &store) == ESP_OK &&
      nvs_get_blob(store, "settings_v1", &saved, &len) == ESP_OK &&
      len == sizeof(saved))
    dot_load_settings(saved);
  applied = dot_settings();
  gpio_config_t cfg = {.pin_bit_mask =
                           (1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_3),
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_DISABLE,
                       .pull_down_en = GPIO_PULLDOWN_DISABLE,
                       .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&cfg));
  for (int i = 0; i < 2; i++) {
    keys[i].stable = keys[i].raw = level(&keys[i]);
    keys[i].armed = !keys[i].stable;
  }
}
void controls_tick(unsigned ms) {
  for (int i = 0; i < 2; i++) {
    Key *k = &keys[i];
    bool down = level(k);
    if (down != k->raw) {
      k->raw = down;
      k->changed = ms;
    }
    if (k->stable == down || ms - k->changed < 30)
      continue;
    k->stable = down;
    if (!k->armed) {
      if (!down)
        k->armed = true;
      continue;
    }
    if (down)
      k->pressed = ms;
    if (i == 0)
      dot_boot_button(down, ms);
    else
      dot_power_key(down, ms); // 0.5.3: the UI times short presses and holds
  }
  DotSettings next = dot_settings();
  if (memcmp(&next, &applied, sizeof(next)) != 0) {
    if (next.bluetooth != applied.bluetooth)
      handset_set_enabled(next.bluetooth);
    applied = next;
    dirty = true;
    modified = ms;
  }
  // Coalesce writes while dragging the two sliders.
  if (store && dirty && ms - modified >= 450) {
    if (nvs_set_blob(store, "settings_v1", &applied, sizeof(applied)) ==
            ESP_OK &&
        nvs_commit(store) == ESP_OK) {
      dirty = false;
      ESP_LOGI("dot_settings",
               "saved volume=%u brightness=%u bt=%u mute=%u boot=%u pwr=%u",
               applied.volume, applied.brightness, applied.bluetooth,
               applied.muted, applied.boot, applied.power);
    } else {
      modified = ms;
      ESP_LOGW("dot_settings", "Cannot save device preferences");
    }
  }
}
void controls_flush(void) {
  DotSettings next = dot_settings();
  if (!store || (!dirty && memcmp(&next, &applied, sizeof(next)) == 0))
    return;
  applied = next;
  if (nvs_set_blob(store, "settings_v1", &applied, sizeof(applied)) == ESP_OK &&
      nvs_commit(store) == ESP_OK)
    dirty = false;
}
