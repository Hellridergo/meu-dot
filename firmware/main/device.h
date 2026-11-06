#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void handset_init(bool enabled);
void handset_set_enabled(bool enabled);
void controls_init(void);
void controls_tick(unsigned ms);
void voice_set_volume(int percent);
void voice_cancel_playback(void);
void device_speech_level(int level);
bool handset_send(const uint8_t *packet, int size);
// Bulk audio. Uses notifications when the phone enabled them, which lets
// several packets share one connection event; otherwise falls back to
// handset_send. Ordering with handset_send is preserved by the ATT bearer.
bool handset_stream(const uint8_t *packet, int size);
// The phone announced it accepts microphone audio while it is being recorded.
bool handset_streams_mic(void);
int handset_payload(void);
void handset_action(int action, uint32_t tag);
bool device_card(const char *s, int type, uint32_t tag);
void voice_init(void);
void voice_action(int action);
bool voice_speaker_packet(const uint8_t *p, int n);
void device_status(bool paired, bool remote);
void device_key(int code);
void device_reply(const char *s, int type);
void device_delivered(void);
void device_level(int level);
uint32_t device_card_tag(void);
void battery_init(void);
// 0.5.3: soft power-off through the AXP2101 (PWR held 3 s). False if the PMU
// was not identified or did not accept the command.
bool battery_power_off(void);
// Saves pending settings at once instead of after the usual 450 ms pause.
void controls_flush(void);
void device_battery(uint8_t flags, uint8_t percent);
void handset_battery(uint8_t flags, uint8_t percent);
void handset_audio_event(uint8_t kind, uint32_t tag, uint32_t elapsed);
// 0.5.2: per-reply speaker report for the phone's log (op 0x34). Durations in
// ms; counts and the worst block interval saturate at 255.
void handset_audio_stats(unsigned received_ms, unsigned played_ms,
                         unsigned dry, unsigned dry_ms, unsigned underruns,
                         unsigned starved_ms, unsigned worst_gap_ms);
