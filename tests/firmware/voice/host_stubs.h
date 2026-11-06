#ifndef DOT_HOST_STUBS_H
#define DOT_HOST_STUBS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define MALLOC_CAP_SPIRAM 1
#define ESP_CODEC_DEV_OK 0
#define ESP_LOGI(tag,fmt,...) log_audio(tag,fmt,__VA_ARGS__)
#define ESP_LOGW(tag,fmt,...) log_audio(tag,fmt,__VA_ARGS__)

typedef struct HostQueue *QueueHandle_t;
typedef struct HostSem *SemaphoreHandle_t;
typedef struct HostTask *TaskHandle_t;
typedef struct HostCodec *esp_codec_dev_handle_t;
typedef struct { int bits_per_sample, channel, sample_rate; } esp_codec_dev_sample_info_t;

QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueSendToFront(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueuePeek(QueueHandle_t,void *,unsigned);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void(*)(void *),const char *,unsigned,void *,unsigned,void *);
int xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int,unsigned);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
void log_audio(const char *,const char *,...);
uint32_t esp_random(void);
void *heap_caps_malloc(size_t,int);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
int esp_codec_dev_open(esp_codec_dev_handle_t,esp_codec_dev_sample_info_t *);
int esp_codec_dev_close(esp_codec_dev_handle_t);
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t,float);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t,int);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t,bool);
int esp_codec_dev_read(esp_codec_dev_handle_t,void *,int);
int esp_codec_dev_write(esp_codec_dev_handle_t,void *,int);
#endif
