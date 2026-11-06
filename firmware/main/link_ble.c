#include "device.h"
#include "dot_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>

void ble_store_config_init(void);
#define UUID(n)                                                                \
  BLE_UUID128_INIT(0xab, 0x87, 0x04, 0x0d, 0x94, 0x2c, 0xea, 0xa6, 0x32, 0x4d, \
                   0xc2, 0x5a, n, 0x00, 0xf1, 0x64)
static const ble_uuid128_t svc = UUID(0), rx = UUID(1), tx = UUID(2);
static uint16_t value_handle;
static atomic_int conn = BLE_HS_CONN_HANDLE_NONE, size_att = 23;
static atomic_bool encrypted, subscribed, accepted, want_radio, caps_sent;
// notify_on: the phone enabled notifications as well as indications on TX.
// peer_mic_stream: the phone answered capability bit 4 of 35 with 08 01.
static atomic_bool notify_on, peer_mic_stream;
static TaskHandle_t radio_manager;
static SemaphoreHandle_t host_exited;
static void advertise_capabilities(void);
static uint8_t addr_type;
static SemaphoreHandle_t lock, ack;
static QueueHandle_t actions;
static uint8_t incoming[1100], card_tag[4];
static int used, expected = -1, card_type;
static int gap(struct ble_gap_event *e, void *arg);
static bool ready(void) {
  return atomic_load(&want_radio) && atomic_load(&encrypted) &&
         atomic_load(&subscribed);
}
int handset_payload(void) { return atomic_load(&size_att) - 3; }
static void adv(void) {
  if (!atomic_load(&want_radio))
    return;
  struct ble_hs_adv_fields f = {0}, r = {0};
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.uuids128 = (ble_uuid128_t *)&svc;
  f.num_uuids128 = 1;
  f.uuids128_is_complete = 1;
  const char *name = "Dot de bolso";
  r.name = (uint8_t *)name;
  r.name_len = strlen(name);
  r.name_is_complete = 1;
  ble_gap_adv_set_fields(&f);
  ble_gap_adv_rsp_set_fields(&r);
  const struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                       .disc_mode = BLE_GAP_DISC_MODE_GEN};
  ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &p, gap, NULL);
}
static int write_data(uint16_t handle, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)handle;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !ready())
    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
  uint8_t b[517];
  uint16_t n;
  if (ble_hs_mbuf_to_flat(ctxt->om, b, sizeof(b), &n) || !n)
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  switch (b[0]) {
  case 1:
    if (n != 2)
      break;
    device_status(true, b[1] != 0);
    // The phone sends its first status only after discovering services, so
    // capabilities sent now cannot be dropped (as on a bond restore, when
    // writing the same CCC value raises no subscribe event).
    caps_sent = false;
    advertise_capabilities();
    return 0;
  case 2:
    if (n != 8 || b[1] > 2)
      break;
    card_type = b[1];
    used = 0;
    expected = b[6] | b[7] << 8;
    if (expected <= 0 || expected >= (int)sizeof(incoming)) {
      expected = -1;
      break;
    }
    memcpy(card_tag, b + 2, 4);
    return 0;
  case 3:
    if (expected < 0 || used + n - 1 > expected)
      break;
    memcpy(incoming + used, b + 1, n - 1);
    used += n - 1;
    return 0;
  case 4:
    if (expected < 1 || used != expected)
      break;
    incoming[used] = 0;
    expected = -1;
    uint32_t tag;
    memcpy(&tag, card_tag, 4);
    return device_card((const char *)incoming, card_type, tag) ? 0 : BLE_ATT_ERR_UNLIKELY;
  case 7:
    if (n != 1)
      break;
    device_delivered();
    return 0;
  case 8:
    // Phone accepts the microphone while recording (answer to 35 bit 2).
    if (n != 2)
      break;
    atomic_store(&peer_mic_stream, (b[1] & 1) != 0);
    return 0;
  case 0x40:
  case 0x41:
  case 0x42:
  case 0x43:
  case 0x44:
  case 0x45:
  case 0x46:
  case 0x47:
    if (voice_speaker_packet(b, n))
      return 0;
    break;
  default:
    break;
  }
  // Speaker packets are reported once per reply by voice.c.
  if (b[0] < 0x40 || b[0] > 0x47)
    ESP_LOGW("dot_ble", "pacote %02x recusado, %u bytes", b[0], n);
  return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_chr_def chars[] = {
    // WRITE_NO_RSP lets the phone queue several speaker packets per connection
    // event. AUTHEN is enforced by NimBLE for both write types, and write_data
    // still requires an encrypted, subscribed link.
    {.uuid = &rx.u,
     .access_cb = write_data,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
              BLE_GATT_CHR_F_WRITE_AUTHEN},
    // Control packets stay on indications. Phones that see NOTIFY may enable
    // both, and microphone audio then uses notifications.
    {.uuid = &tx.u,
     .access_cb = write_data,
     .val_handle = &value_handle,
     .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY},
    {0}};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &svc.u,
     .characteristics = chars},
    {0}};
static int gap(struct ble_gap_event *e, void *arg) {
  (void)arg;
  switch (e->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (e->connect.status) {
      adv();
      break;
    }
    conn = e->connect.conn_handle;
    caps_sent = false;
    size_att = 23;
    encrypted = false;
    subscribed = false;
    notify_on = false;
    peer_mic_stream = false;
    ble_gap_security_initiate(conn);
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    if (e->passkey.params.action == BLE_SM_IOACT_DISP) {
      struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP,
                             .passkey = esp_random() % 1000000};
      device_key(io.passkey);
      ble_sm_inject_io(e->passkey.conn_handle, &io);
    }
    break;
  case BLE_GAP_EVENT_ENC_CHANGE: {
    struct ble_gap_conn_desc d;
    encrypted = !e->enc_change.status &&
                !ble_gap_conn_find(e->enc_change.conn_handle, &d) &&
                d.sec_state.encrypted && d.sec_state.authenticated;
    if (encrypted) {
      device_key(-1);
      device_status(ready(), false);
      advertise_capabilities();
    }
    break;
  }
  case BLE_GAP_EVENT_SUBSCRIBE:
    if (e->subscribe.attr_handle == value_handle) {
      // A phone that saw the fast-link bit rewrites the CCC to add notifications.
      // That must not reset the online state already sent with op 1.
      bool was = atomic_exchange(&subscribed, e->subscribe.cur_indicate);
      notify_on = e->subscribe.cur_notify;
      if (was != (bool)e->subscribe.cur_indicate)
        device_status(ready(), false);
      // On a bonded reconnect NimBLE restores the CCC at encryption, before
      // Android has discovered services, and Android drops indications sent
      // then. Repeat the capabilities when the phone itself subscribes, but
      // not after the notification upgrade.
      if (e->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_WRITE &&
          !e->subscribe.cur_notify)
        caps_sent = false;
      advertise_capabilities();
    }
    break;
  case BLE_GAP_EVENT_MTU:
    size_att = e->mtu.value;
    break;
  case BLE_GAP_EVENT_NOTIFY_TX:
    if (e->notify_tx.indication && e->notify_tx.status != 0) {
      accepted = e->notify_tx.status == BLE_HS_EDONE;
      xSemaphoreGive(ack);
    }
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    // 0x208 supervision timeout, 0x213 ended by the phone, 0x216 by the board.
    ESP_LOGW("dot_ble", "desconectado, motivo 0x%03x", e->disconnect.reason);
    conn = BLE_HS_CONN_HANDLE_NONE;
    encrypted = false;
    subscribed = false;
    notify_on = false;
    peer_mic_stream = false;
    expected = -1;
    accepted = false;
    xSemaphoreGive(ack);
    voice_cancel_playback();
    voice_action(MIC_DISCARD);
    device_key(-1);
    device_status(false, false);
    adv();
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    // Replacing a saved key requires seeing the newly displayed passkey.
    {
      struct ble_gap_conn_desc d;
      if (!ble_gap_conn_find(e->repeat_pairing.conn_handle, &d))
        ble_store_util_delete_peer(&d.peer_id_addr);
    }
    return BLE_GAP_REPEAT_PAIRING_RETRY;
  default:
    break;
  }
  return 0;
}
bool handset_send(const uint8_t *packet, int size) {
  if (!ready() || size > handset_payload() ||
      xSemaphoreTake(lock, pdMS_TO_TICKS(2500)) != pdTRUE)
    return false;
  bool ok = false;
  int original = conn;
  if (ready() && original != BLE_HS_CONN_HANDLE_NONE) {
    xSemaphoreTake(ack, 0);
    accepted = false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, size);
    if (om && ble_gatts_indicate_custom(original, value_handle, om) == 0) {
      // While the phone streams a reply, its confirmation can wait behind the
      // write commands. Ending the link after 3 s cut replies mid-sentence. A
      // dead link still ends by supervision timeout or NimBLE's 30 s ATT
      // timeout; a late confirmation is cleared by the next send.
      ok = xSemaphoreTake(ack, pdMS_TO_TICKS(8000)) == pdTRUE && accepted &&
           conn == original;
      if (!ok && conn == original)
        ESP_LOGW("dot_ble", "aviso %02x sem confirmação em 8 s", packet[0]);
    }
  }
  xSemaphoreGive(lock);
  return ok;
}
bool handset_streams_mic(void) {
  return ready() && atomic_load(&notify_on) && atomic_load(&peer_mic_stream);
}
bool handset_stream(const uint8_t *packet, int size) {
  if (!atomic_load(&notify_on))
    return handset_send(packet, size);
  if (!ready() || size > handset_payload() ||
      xSemaphoreTake(lock, pdMS_TO_TICKS(2500)) != pdTRUE)
    return false;
  bool ok = false;
  int original = conn;
  // Notifications need no confirmation, so the controller can pack several
  // into each connection event. When the host runs out of buffers, wait for
  // the link to drain instead of failing the recording.
  for (int attempt = 0; attempt < 500; attempt++) {
    if (!ready() || original == BLE_HS_CONN_HANDLE_NONE || conn != original)
      break;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, size);
    int rc = om ? ble_gatts_notify_custom(original, value_handle, om)
                : BLE_HS_ENOMEM;
    if (rc == 0) {
      ok = true;
      break;
    }
    if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY)
      break;
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  xSemaphoreGive(lock);
  return ok;
}
typedef struct { uint8_t bytes[12], size; } Choice;
static atomic_uint last_battery = 255u << 8;
void handset_action(int action, uint32_t tag) {
  if (!actions)
    return;
  Choice c = {.bytes = {0x21, (uint8_t)action}, .size = 6};
  memcpy(c.bytes + 2, &tag, 4);
  xQueueSend(actions, &c, 0);
}
void handset_battery(uint8_t flags, uint8_t percent) {
  atomic_store(&last_battery, flags | ((unsigned)percent << 8));
  if (!actions || !ready()) return;
  Choice c = {.bytes = {0x31, 1, flags, percent}, .size = 4};
  xQueueSend(actions, &c, 0);
}
void handset_audio_event(uint8_t kind, uint32_t tag, uint32_t elapsed) {
  if (!actions || !ready()) return;
  Choice c = {.bytes = {0x32, kind}, .size = 10};
  memcpy(c.bytes + 2, &tag, 4);
  memcpy(c.bytes + 6, &elapsed, 4);
  // Never block the audio task on an indication. Losing a receipt leaves the
  // notice pending for manual dismissal; it cannot approve a decision.
  xQueueSend(actions, &c, 0);
}
static void put16(uint8_t *at, unsigned v) {
  v = v > 65535 ? 65535 : v;
  at[0] = v;
  at[1] = v >> 8;
}
void handset_audio_stats(unsigned received_ms, unsigned played_ms,
                         unsigned dry, unsigned dry_ms, unsigned underruns,
                         unsigned starved_ms, unsigned worst_gap_ms) {
  if (!actions || !ready()) return;
  // 34 received_ms:2 played_ms:2 dry:1 dry_ms:2 underruns:1 starved_ms:2 gap:1
  Choice c = {.bytes = {0x34}, .size = 12};
  put16(c.bytes + 1, received_ms);
  put16(c.bytes + 3, played_ms);
  c.bytes[5] = dry > 255 ? 255 : dry;
  put16(c.bytes + 6, dry_ms);
  c.bytes[8] = underruns > 255 ? 255 : underruns;
  put16(c.bytes + 9, starved_ms);
  c.bytes[11] = worst_gap_ms > 255 ? 255 : worst_gap_ms;
  // Diagnostics only: drop it rather than wait when the queue is full.
  xQueueSend(actions, &c, 0);
}
static void advertise_capabilities(void) {
  if (!actions || !ready() || atomic_exchange(&caps_sent, true))
    return;
  Choice cap = {.bytes = {0x30, 1}, .size = 2};
  if (xQueueSend(actions, &cap, 0) != pdTRUE)
    caps_sent = false;
  // 35 01 flags. Bit 2 (0.4.2): fast link. Phones that understand it enable
  // notifications on TX, write speaker audio without response and reply 08 01
  // to receive the microphone while it records (13/11/14/16). Older Meu Dot
  // phones read only bit 0; Dot de bolso 0.3 phones ignore 35.
  cap = (Choice){.bytes = {0x35, 1, 3 | 4}, .size = 3};
  if (xQueueSend(actions, &cap, 0) != pdTRUE) caps_sent = false;
  unsigned b = atomic_load(&last_battery);
  handset_battery(b & 255, b >> 8);
}
static void choices(void *arg) {
  Choice c;
  for (;;) {
    if (xQueueReceive(actions, &c, portMAX_DELAY) != pdTRUE)
      continue;
    if (!handset_send(c.bytes, c.size) &&
        (c.bytes[0] == 0x30 || c.bytes[0] == 0x35))
      caps_sent = false;
  }
}
static void synced(void) {
  ble_hs_id_infer_auto(0, &addr_type);
  adv();
}
static void host(void *arg) {
  nimble_port_run();
  xSemaphoreGive(host_exited);
  vTaskDelete(NULL);
}
static esp_err_t start_radio(void) {
  esp_err_t result = nimble_port_init();
  if (result != ESP_OK)
    return result;
  ble_hs_cfg.sync_cb = synced;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = ble_hs_cfg.sm_our_key_dist;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set("Dot de bolso");
  if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) {
    nimble_port_deinit();
    return ESP_FAIL;
  }
  ble_store_config_init();
  if (xTaskCreatePinnedToCore(host, "dot_ble", NIMBLE_HS_STACK_SIZE, NULL,
                              configMAX_PRIORITIES - 4, NULL,
                              NIMBLE_CORE) != pdPASS) {
    nimble_port_deinit();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
static void manage_radio(void *arg) {
  bool running = false;
  for (;;) {
    bool desired = atomic_load(&want_radio);
    if (desired && !running) {
      esp_err_t r = start_radio();
      running = r == ESP_OK;
      if (!running) {
        ESP_LOGE("dot_ble", "Start failed: %s", esp_err_to_name(r));
        device_reply("Falha ao ligar o Bluetooth.", 2);
      }
    } else if (!desired && running) {
      // Do not tear down the controller under an unacknowledged indication.
      xSemaphoreTake(lock, portMAX_DELAY);
      int result = nimble_port_stop();
      if (result == 0) {
        xSemaphoreTake(host_exited, portMAX_DELAY);
        esp_err_t end = nimble_port_deinit();
        running = false;
        if (end != ESP_OK)
          ESP_LOGE("dot_ble", "Stop failed: %s", esp_err_to_name(end));
      }
      xSemaphoreGive(lock);
      if (running)
        ESP_LOGW("dot_ble", "Stop returned %d", result);
      encrypted = false;
      subscribed = false;
      caps_sent = false;
      device_status(false, false);
    }
    ulTaskNotifyTake(pdTRUE, running == atomic_load(&want_radio)
                                 ? portMAX_DELAY
                                 : pdMS_TO_TICKS(2000));
  }
}
void handset_set_enabled(bool enabled) {
  atomic_store(&want_radio, enabled);
  if (radio_manager)
    xTaskNotifyGive(radio_manager);
}
void handset_init(bool enabled) {
  lock = xSemaphoreCreateMutex();
  ack = xSemaphoreCreateBinary();
  host_exited = xSemaphoreCreateBinary();
  actions = xQueueCreate(16, sizeof(Choice));
  want_radio = enabled;
  xTaskCreate(choices, "dot_choices", 4096, NULL, 3, NULL);
  xTaskCreate(manage_radio, "dot_radio", 6144, NULL, 4, &radio_manager);
}
