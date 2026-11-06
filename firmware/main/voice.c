#include "bsp/esp-bsp.h"
#include "device.h"
#include "dot_ui.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>
#define RATE 16000
#define BLOCK 320
#define RECORD_CAP (RATE * DOT_MAX_RECORD_SECONDS)
#define SPEAK_CAP (RATE * 30)
static uint8_t *recording, *playback;
static esp_codec_dev_handle_t mic, speaker;
static QueueHandle_t events;
// GATT provides ordered writes. Keep a bounded PSRAM clip so old phones can
// send complete files, new phones can stream, and a completed reply can be
// replayed.
static struct {
  unsigned generation, used, expected;
  uint32_t tag, response_started, opened_at;
  bool open, streaming, ended, scheduled;
} clip;
static SemaphoreHandle_t clip_lock;
static atomic_int speaker_volume = 58;
static atomic_uint released_at;
static atomic_uint recording_id;
static uint32_t bound_tag, bound_recording;
static bool binding_valid;
typedef struct {
  int kind;
  unsigned generation;
} AudioEvent;
enum { PLAY = 100 };
static unsigned milliseconds(void) {
  return (unsigned)(esp_timer_get_time() / 1000);
}
void voice_set_volume(int percent) { atomic_store(&speaker_volume, percent); }
void voice_cancel_playback(void) {
  if (!clip_lock)
    return;
  xSemaphoreTake(clip_lock, portMAX_DELAY);
  clip.generation++;
  clip.open = false;
  clip.ended = false;
  clip.scheduled = false;
  binding_valid = false;
  xSemaphoreGive(clip_lock);
}
static uint8_t encode(int v) {
  int mask = v < 0 ? 0x7f : 0xff;
  if (v < 0)
    v = -v;
  if (v > 32635)
    v = 32635;
  v += 132;
  int e = 7;
  for (int bit = 0x4000; e > 0 && !(v & bit); bit >>= 1)
    e--;
  return ((e << 4) | ((v >> (e + 3)) & 15)) ^ mask;
}
static int16_t decode(uint8_t u) {
  u = ~u;
  int v = (((u & 15) << 3) + 132) << ((u >> 4) & 7);
  return u & 128 ? 132 - v : v - 132;
}
static esp_codec_dev_sample_info_t fmt = {
    .bits_per_sample = 16, .channel = 1, .sample_rate = 16000};
static void fail(const char *s) { device_reply(s, 2); }
static bool send_clip(unsigned n) {
  uint32_t id = esp_random();
  atomic_store(&recording_id, id);
  uint8_t packet[247] = {0x10};
  memcpy(packet + 1, &n, 4);
  memcpy(packet + 5, &id, 4);
  if (!handset_send(packet, 9))
    return false;
  int chunk = handset_payload() - 3;
  if (chunk > (int)sizeof(packet) - 3)
    chunk = sizeof(packet) - 3;
  if (chunk < 14)
    return false;
  uint16_t seq = 0;
  for (unsigned at = 0; at < n;) {
    int count = n - at < (unsigned)chunk ? (int)(n - at) : chunk;
    packet[0] = 0x11;
    packet[1] = seq;
    packet[2] = seq >> 8;
    memcpy(packet + 3, recording + at, count);
    if (!handset_send(packet, count + 3))
      return false;
    at += count;
    seq++;
  }
  packet[0] = 0x12;
  memcpy(packet + 1, &id, 4);
  return handset_send(packet, 5);
}
// Microphone audio forwarded while it is recorded, for phones that answered
// 31 01: 13 id | 11 seq data ... | 14 id total tail_ms, or 16 id if dropped.
// The capture loop only appends to `recording`; this task sends from it, so a
// slow radio never stalls the I2S reads. At release only the last packet or
// two remain, instead of the whole clip.
enum { UPLINK_LIVE = 1, UPLINK_SEND, UPLINK_DROP };
static TaskHandle_t uplink_task;
static SemaphoreHandle_t uplink_done;
static atomic_uint uplink_have;
static atomic_int uplink_state;
static atomic_bool uplink_pending, uplink_ok;
static void uplink_wake(void) {
  if (uplink_task)
    xTaskNotifyGive(uplink_task);
}
static bool stream_clip(void) {
  uint32_t id = esp_random();
  // Same id the phone binds to its reply with 47, as in send_clip.
  atomic_store(&recording_id, id);
  uint8_t packet[247] = {0x13};
  memcpy(packet + 1, &id, 4);
  int chunk = handset_payload() - 3;
  if (chunk > (int)sizeof(packet) - 3)
    chunk = sizeof(packet) - 3;
  bool ok = chunk >= 14 && handset_send(packet, 5);
  unsigned sent = 0;
  uint16_t seq = 0;
  for (;;) {
    // Read the state before the count: capture publishes the final count
    // before it switches the state to SEND.
    int state = atomic_load(&uplink_state);
    unsigned have = atomic_load(&uplink_have);
    if (state == UPLINK_DROP) {
      if (ok) {
        packet[0] = 0x16;
        memcpy(packet + 1, &id, 4);
        handset_send(packet, 5);
      }
      return false;
    }
    unsigned pending = have - sent;
    if (ok && pending &&
        (pending >= (unsigned)chunk || state == UPLINK_SEND)) {
      int count = pending < (unsigned)chunk ? (int)pending : chunk;
      packet[0] = 0x11;
      packet[1] = seq;
      packet[2] = seq >> 8;
      memcpy(packet + 3, recording + sent, count);
      ok = handset_stream(packet, count + 3);
      sent += count;
      seq++;
      continue;
    }
    if (state == UPLINK_SEND) {
      if (!ok)
        return false;
      unsigned released = atomic_load(&released_at);
      unsigned tail = released ? milliseconds() - released : 0;
      if (tail > 65535)
        tail = 65535;
      packet[0] = 0x14;
      memcpy(packet + 1, &id, 4);
      memcpy(packet + 5, &sent, 4);
      packet[9] = tail;
      packet[10] = tail >> 8;
      return handset_send(packet, 11);
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
  }
}
static void uplink(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!atomic_exchange(&uplink_pending, false))
      continue;
    atomic_store(&uplink_ok, stream_clip());
    xSemaphoreGive(uplink_done);
  }
}
// armed: the touch has not become hold-to-talk yet. Record silently from
// touch-down so the first syllable survives the 260 ms hold; MIC_START
// confirms, MIC_DISARM drops it without any message.
static int capture(bool armed) {
  bool confirmed = !armed;
  if (confirmed && handset_payload() < 64) {
    fail("Reconecte o Bluetooth no celular.");
    return MIC_DISCARD;
  }
  if (!recording || !mic || esp_codec_dev_open(mic, &fmt) != ESP_CODEC_DEV_OK) {
    if (confirmed)
      fail("Não consegui abrir o microfone.");
    return MIC_DISCARD;
  }
  esp_codec_dev_set_in_gain(mic, 24.0f);
  unsigned n = 0, armed_at = milliseconds();
  int action = NONE;
  bool streaming = false;
  int16_t pcm[BLOCK];
  int warm = 3;
  for (;;) {
    AudioEvent next;
    if (xQueueReceive(events, &next, 0) == pdTRUE) {
      if (next.kind == MIC_START && !confirmed) {
        confirmed = true;
        if (handset_payload() < 64) {
          fail("Reconecte o Bluetooth no celular.");
          action = MIC_DISCARD;
          break;
        }
      } else if (next.kind == MIC_SEND) {
        action = confirmed ? MIC_SEND : MIC_DISCARD;
        break;
      } else if (next.kind == MIC_DISCARD || next.kind == MIC_DISARM) {
        action = MIC_DISCARD;
        break;
      } else if (next.kind == PLAY) {
        if (confirmed)
          voice_cancel_playback();
        else {
          // A reply arrived before the touch became a recording: play it.
          // If the queue is full, let the next speaker packet schedule it.
          if (xQueueSendToFront(events, &next, 0) != pdTRUE) {
            xSemaphoreTake(clip_lock, portMAX_DELAY);
            if (clip.generation == next.generation)
              clip.scheduled = false;
            xSemaphoreGive(clip_lock);
          }
          action = MIC_DISCARD;
          break;
        }
      }
    }
    if (!confirmed && milliseconds() - armed_at > 2000) {
      action = MIC_DISCARD;
      break;
    }
    if (esp_codec_dev_read(mic, pcm, sizeof(pcm)) != ESP_CODEC_DEV_OK) {
      action = MIC_DISCARD;
      if (confirmed)
        fail("Falha ao ler os microfones.");
      break;
    }
    if (warm) {
      warm--;
      continue;
    }
    int peak = 0;
    for (int i = 0; i < BLOCK && n < RECORD_CAP; i++) {
      int val = pcm[i];
      int a = val < 0 ? -val : val;
      if (a > peak)
        peak = a;
      recording[n++] = encode(val);
    }
    if (confirmed)
      device_level(peak * 100 / 12500);
    atomic_store(&uplink_have, n);
    if (streaming)
      uplink_wake();
    else if (confirmed && n >= RATE / 4 && uplink_task &&
             handset_streams_mic()) {
      // Start once the clip can no longer be "too short", so taps and quick
      // releases never reach the phone.
      streaming = true;
      atomic_store(&uplink_state, UPLINK_LIVE);
      atomic_store(&uplink_pending, true);
      uplink_wake();
    }
    if (n >= RECORD_CAP) {
      action = confirmed ? MIC_SEND : MIC_DISCARD;
      break;
    }
  }
  esp_codec_dev_close(mic);
  device_level(0);
  if (action == MIC_SEND && !atomic_load(&released_at))
    atomic_store(&released_at, milliseconds());
  if (streaming) {
    atomic_store(&uplink_have, n);
    atomic_store(&uplink_state, action == MIC_SEND ? UPLINK_SEND : UPLINK_DROP);
    uplink_wake();
    // The uplink reads `recording`; keep it intact until it finishes. Every
    // radio call it makes is bounded by its own timeout.
    xSemaphoreTake(uplink_done, portMAX_DELAY);
    if (action == MIC_SEND && !atomic_load(&uplink_ok))
      fail("Envio interrompido. Confira o celular.");
  } else if (action == MIC_SEND) {
    if (n < RATE / 4)
      fail("O trecho foi curto. Fale novamente.");
    else if (!send_clip(n))
      fail("Envio interrompido. Confira o celular.");
  }
  memset(recording, 0, n);
  return action;
}
static void play(unsigned generation) {
  unsigned at = 0, stalled = milliseconds();
  int current_volume = -1;
  bool opened = false, buffering = true, completed = false, first = false;
  // Underruns while the reply is still streaming. After the first one, wait
  // for 1.5 s of audio (0.5 s until 0.5.3), so a slow stretch causes one
  // pause instead of a series of short cuts.
  unsigned underruns = 0, starved_at = 0, starved_ms = 0;
  const char *why = NULL;
  xSemaphoreTake(clip_lock, portMAX_DELAY);
  uint32_t tag = clip.tag, started = clip.response_started;
  clip.response_started = 0; // Replaying a saved reply is not a new latency sample.
  xSemaphoreGive(clip_lock);
  uint8_t encoded[BLOCK];
  int16_t pcm[BLOCK];
  // 0.5.1: speaker timing. Before each block, `queued` estimates the audio the
  // I2S DMA still holds: the samples written since `clock_us` minus the time
  // elapsed. Below -10 ms the DMA ran dry and played silence. That cut is
  // invisible to the counters above, because every byte had already arrived.
  int64_t clock_us = 0, clock_samples = 0, first_us = 0, last_us = 0;
  int margin_ms = INT32_MAX;
  unsigned dry = 0, dry_ms = 0, worst_gap_ms = 0, received = 0;
  while (speaker) {
    AudioEvent next;
    if (xQueuePeek(events, &next, 0) == pdTRUE) {
      // A stray disarm must not sit at the head and hide a later MIC_START.
      if (next.kind == MIC_DISARM)
        xQueueReceive(events, &next, 0);
      else if (next.kind == MIC_START || next.kind == MIC_ARM ||
               next.kind == MIC_DISCARD || next.kind == PLAY)
        break;
    }
    xSemaphoreTake(clip_lock, portMAX_DELAY);
    if (generation != clip.generation || !clip.open) {
      xSemaphoreGive(clip_lock);
      break;
    }
    unsigned available = clip.used - at;
    bool end = clip.ended;
    unsigned take = available > BLOCK ? BLOCK : available;
    // 0.5.4: audio arriving slower than 1.25x real time (Bluetooth shared with
    // other devices, a fresh connection) starts only with 1 s buffered. Measured
    // from the stream's opening; a complete clip or a replay is never held.
    unsigned since = milliseconds() - clip.opened_at;
    bool slow = since >= 150 && (uint64_t)clip.used * 1000 < (uint64_t)since * (RATE * 5 / 4);
    unsigned need = underruns ? RATE * 3 / 2 : slow ? RATE : RATE / 5;
    if (!end && (available < BLOCK || (buffering && available < need)))
      take = 0;
    if (!take && !end && milliseconds() - stalled > 15000) {
      why = "15 s sem dados";
      // This generation lost its consumer. Reject late packets until a fresh
      // START so the phone can report the interruption instead of losing audio.
      clip.open = false;
      clip.scheduled = false;
      xSemaphoreGive(clip_lock);
      break;
    }
    if (take)
      memcpy(encoded, playback + at, take);
    xSemaphoreGive(clip_lock);
    if (!take) {
      if (end) {
        completed = true;
        break;
      }
      if (!buffering && at) {
        underruns++;
        starved_at = milliseconds();
      }
      buffering = true;
      // Waiting for data is counted above; restart the speaker clock after it.
      clock_us = last_us = 0;
      device_speech_level(0);
      vTaskDelay(pdMS_TO_TICKS(8));
      continue;
    }
    if (!opened) {
      if (esp_codec_dev_open(speaker, &fmt) != ESP_CODEC_DEV_OK) {
        why = "codec não abriu";
        break;
      }
      opened = true;
    }
    int volume = atomic_load(&speaker_volume);
    if (current_volume != volume) {
      esp_codec_dev_set_out_vol(speaker, volume);
      esp_codec_dev_set_out_mute(speaker, volume == 0);
      current_volume = volume;
    }
    int peak = 0;
    for (unsigned i = 0; i < take; i++) {
      pcm[i] = decode(encoded[i]);
      int level = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
      if (level > peak)
        peak = level;
    }
    // Opening or reconfiguring the codec can block. Drop a prepared block if
    // GATT cancelled or replaced this clip while the codec was being opened.
    xSemaphoreTake(clip_lock, portMAX_DELAY);
    bool current = generation == clip.generation && clip.open;
    xSemaphoreGive(clip_lock);
    if (!current) break;
    device_speech_level(volume ? (peak >= 16000 ? 100 : peak * 100 / 16000) : 0);
    int64_t t = esp_timer_get_time();
    if (clock_us) {
      int64_t queued = clock_samples * 1000000 / RATE - (t - clock_us);
      if (queued / 1000 < margin_ms)
        margin_ms = (int)(queued / 1000);
      if (queued < -10000) {
        dry++;
        dry_ms += (unsigned)(-queued / 1000);
        clock_us = t; // the DMA restarts from this block
        clock_samples = 0;
      }
    } else {
      clock_us = t;
      clock_samples = 0;
    }
    if (last_us && (unsigned)((t - last_us) / 1000) > worst_gap_ms)
      worst_gap_ms = (unsigned)((t - last_us) / 1000);
    if (!first_us)
      first_us = t;
    last_us = t;
    if (esp_codec_dev_write(speaker, pcm, take * sizeof(pcm[0])) !=
        ESP_CODEC_DEV_OK) {
      why = "falha ao escrever no codec";
      break;
    }
    clock_samples += take;
    if (peak > 160 && volume > 0 && !first) {
      first = true;
      unsigned elapsed = started ? milliseconds() - started : 0;
      handset_audio_event(1, tag, elapsed);
      if (started)
        ESP_LOGI("dot_tempo", "tag=%lu soltar_ate_primeiro_bloco_ms=%u",
                 (unsigned long)tag, elapsed);
    }
    at += take;
    if (starved_at) {
      starved_ms += milliseconds() - starved_at;
      starved_at = 0;
    }
    buffering = false;
    stalled = milliseconds();
  }
  if (starved_at)
    starved_ms += milliseconds() - starved_at;
  if (underruns || why)
    ESP_LOGW("dot_audio", "resposta: %u pausas, %u ms sem dados%s%s", underruns,
             starved_ms, why ? ", parou: " : "", why ? why : "");
  if (opened && esp_codec_dev_close(speaker) != ESP_CODEC_DEV_OK)
    completed = false;
  xSemaphoreTake(clip_lock, portMAX_DELAY);
  if (generation == clip.generation)
    received = clip.used;
  completed = completed && generation == clip.generation && clip.open;
  if (generation == clip.generation && !clip.ended) {
    clip.open = false;
    clip.scheduled = false;
  }
  xSemaphoreGive(clip_lock);
  if (at)
    ESP_LOGI("dot_audio",
             "voz: %u bytes recebidos, %u ms tocados em %u ms, folga mínima "
             "%d ms, %u falhas no alto-falante (%u ms), maior intervalo %u ms",
             received, at * 1000 / RATE,
             first_us ? (unsigned)((last_us - first_us) / 1000) : 0,
             margin_ms == INT32_MAX ? 0 : margin_ms, dry, dry_ms, worst_gap_ms);
  if (at)
    handset_audio_stats(received * 1000 / RATE, at * 1000 / RATE, dry, dry_ms,
                        underruns, starved_ms, worst_gap_ms);
  // Same board clock and release marker as the first non-silent codec write.
  handset_audio_event(completed ? 2 : 3, tag, started ? milliseconds() - started : 0);
  device_speech_level(0);
}
static void audio_thread(void *arg) {
  AudioEvent event;
  for (;;) {
    xQueueReceive(events, &event, portMAX_DELAY);
    if (event.kind == MIC_START || event.kind == MIC_ARM)
      capture(event.kind == MIC_ARM);
    else if (event.kind == PLAY)
      play(event.generation);
  }
}
void voice_action(int action) {
  if (!events)
    return;
  AudioEvent event = {.kind = action};
  if (action == PLAY_LAST) {
    xSemaphoreTake(clip_lock, portMAX_DELAY);
    bool saved = clip.open && clip.ended && clip.used > 0;
    if (saved) {
      event.kind = PLAY;
      event.generation = ++clip.generation;
    }
    xSemaphoreGive(clip_lock);
    if (!saved)
      return;
  } else if (action == MIC_START || action == MIC_ARM)
    atomic_store(&released_at, 0);
  else if (action == MIC_SEND)
    atomic_store(&released_at, milliseconds());
  xQueueSend(events, &event, 0);
}
bool voice_speaker_packet(const uint8_t *p, int n) {
  if (!playback || !clip_lock || !events || n < 1)
    return false;
  uint8_t op = p[0];
  if (op == 0x46 && n == 1) {
    voice_cancel_playback();
    return true;
  }
  xSemaphoreTake(clip_lock, portMAX_DELAY);
  bool ok = false;
  if (op == 0x47 && n == 9) {
    memcpy(&bound_tag, p + 1, 4);
    memcpy(&bound_recording, p + 5, 4);
    binding_valid = true;
    ok = true;
  } else if ((op == 0x40 && n == 5) || (op == 0x43 && n == 1)) {
    uint32_t len = SPEAK_CAP;
    if (op == 0x40)
      memcpy(&len, p + 1, sizeof(len));
    if (len && len <= SPEAK_CAP) {
      clip.generation++;
      clip.used = 0;
      clip.expected = len;
      clip.open = true;
      clip.ended = false;
      clip.scheduled = false;
      clip.streaming = op == 0x43;
      clip.opened_at = milliseconds();
      clip.tag = device_card_tag();
      clip.response_started = binding_valid && clip.tag == bound_tag &&
                                     bound_recording == atomic_load(&recording_id)
                                 ? atomic_load(&released_at) : 0;
      bound_tag = bound_recording = 0;
      binding_valid = false;
      ok = true;
    }
  } else if (clip.open && !clip.ended) {
    const int data = clip.streaming ? 0x44 : 0x41;
    const int end = clip.streaming ? 0x45 : 0x42;
    if (op == data && n > 1 && (unsigned)(n - 1) <= clip.expected - clip.used) {
      memcpy(playback + clip.used, p + 1, n - 1);
      clip.used += n - 1;
      ok = true;
    } else if (op == end && n == 1 && clip.used &&
               (clip.streaming || clip.used == clip.expected)) {
      clip.ended = true;
      ok = true;
    }
    if (ok && !clip.scheduled &&
        (clip.ended || (clip.streaming && clip.used >= RATE / 5))) {
      AudioEvent play_event = {PLAY, clip.generation};
      ok = xQueueSend(events, &play_event, 0) == pdTRUE;
      clip.scheduled = ok;
    }
  }
  unsigned generation_now = clip.generation;
  bool closed = !clip.open || clip.ended;
  xSemaphoreGive(clip_lock);
  // Once per reply: why the phone's speaker packets stopped being accepted.
  static unsigned rejected_generation = ~0u;
  if (!ok && op >= 0x40 && op <= 0x47 && generation_now != rejected_generation) {
    rejected_generation = generation_now;
    ESP_LOGW("dot_audio", "pacote de voz %02x recusado: %s", op,
             closed ? "clipe encerrado" : "tamanho ou fila");
  }
  return ok;
}
void voice_init(void) {
  recording = heap_caps_malloc(RECORD_CAP, MALLOC_CAP_SPIRAM);
  playback = heap_caps_malloc(SPEAK_CAP, MALLOC_CAP_SPIRAM);
  mic = bsp_audio_codec_microphone_init();
  speaker = bsp_audio_codec_speaker_init();
  clip_lock = xSemaphoreCreateMutex();
  events = xQueueCreate(12, sizeof(AudioEvent));
  uplink_done = xSemaphoreCreateBinary();
  if (uplink_done &&
      xTaskCreate(uplink, "dot_uplink", 4096, NULL, 4, &uplink_task) != pdPASS)
    uplink_task = NULL;
  // 0.5.1: above the LVGL task (6). The I2S DMA holds about 90 ms, so a paint
  // or panel flush on the free core must never delay the next block that long.
  // The loop always blocks on the codec or on an 8 ms delay, so it cannot
  // starve the display.
  xTaskCreate(audio_thread, "dot_audio", 7168, NULL, 8, NULL);
}
