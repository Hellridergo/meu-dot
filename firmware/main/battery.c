#include "battery_state.h"
#include "bsp/esp-bsp.h"
#include "device.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t pmu;
static volatile bool pmu_ready;
static bool read_reg(uint8_t reg, uint8_t *out, size_t count) {
  return i2c_master_transmit_receive(pmu, &reg, 1, out, count, 25) == ESP_OK;
}
// 0.5.3: the one write to the PMU. REG 10h bit 0 is the AXP2101 soft power-off;
// PWR (PWRON) turns the board back on. Charger and rails are never changed.
bool battery_power_off(void) {
  uint8_t common = 0;
  if (!pmu_ready || !read_reg(0x10, &common, 1))
    return false;
  uint8_t command[2] = {0x10, (uint8_t)(common | 1)};
  return i2c_master_transmit(pmu, command, sizeof(command), 50) == ESP_OK;
}
static void monitor(void *arg) {
  (void)arg;
  // Reuse the BSP bus (SDA 15/SCL 14). Only battery_power_off writes the PMU.
  i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                               .device_address = 0x34, .scl_speed_hz = 100000};
  esp_err_t result = i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &pmu);
  bool identified = false;
  unsigned round = 0;
  BatteryState previous = {0, 255};
  for (;;) {
    uint8_t id = 0, status[2] = {0}, gauge_control = 0, percent = 255;
    BatteryState b = {0, 255};
    if (result == ESP_OK) {
      if (!identified) {
        identified = read_reg(0x03, &id, 1) && id == 0x4a;
        uint8_t off = 0, levels = 0;
        // How the PMU itself treats a long PWR hold (logged once, read only).
        if (identified && read_reg(0x22, &off, 1) && read_reg(0x27, &levels, 1))
          ESP_LOGI("dot_battery", "pmu axp2101 pwroff_en=0x%02x pwrkey_levels=0x%02x", off,
                   levels);
        pmu_ready = identified;
      }
      if (identified && read_reg(0x00, status, 2)) {
        // A disabled gauge must not turn an old/default register into a
        // believable percentage. Its enable bit is read, never changed.
        if (read_reg(0x18, &gauge_control, 1) && (gauge_control & (1 << 3)))
          if (!read_reg(0xa4, &percent, 1)) percent = 255;
        b = battery_decode(status[0], status[1], percent);
      }
    }
    device_battery(b.flags, b.percent);
    if (b.flags != previous.flags || b.percent != previous.percent || round++ % 6 == 0)
      handset_battery(b.flags, b.percent);
    previous = b;
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
void battery_init(void) {
  if (xTaskCreate(monitor, "dot_battery", 3072, NULL, 1, NULL) != pdPASS)
    ESP_LOGW("dot_battery", "Battery monitor unavailable");
}
