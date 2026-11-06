#include "host_stubs.h"
#include "device.h"
#include "dot_ui.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(e) do { if (!(e)) { fprintf(stderr, "FAILED %s:%d: %s\n", __func__, __LINE__, #e); exit(1); } } while (0)
enum { CAP = 480000, CHUNK = 240, BLOCK = 320 };
struct HostQueue { unsigned capacity, width, first, count; uint8_t bytes[12][32]; };
struct HostSem { bool held, binary; int count; };
struct HostTask { int unused; };
struct HostCodec { bool opened, muted; int volume; };
static struct HostQueue queue;
static struct HostSem sem, done_sem = {.binary = true};
static struct HostTask uplink_host;
// The uplink task is a real thread; the audio worker stays cooperative.
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static unsigned notified;
static atomic_bool phone_streams, live_open, live_done, live_dropped, live_started;
static atomic_uint live_bytes, live_seq, live_tail;
static uint32_t live_id;
static void (*on_read)(void);
static struct HostCodec mic, speaker;
static _Atomic uint64_t ms = 1000;
static void (*worker)(void *), (*on_delay)(void), (*on_write)(void), (*on_open)(void);
static void *worker_arg;
static jmp_buf idle;
static bool in_worker, end_sent, first_before_end, xfer_open, xfer_done;
static unsigned samples, outputs, errors, mic_reads;
static unsigned xfer_bytes, xfer_size, xfer_seq, xfer_id;
static uint32_t current_tag = 0x76543210;
static unsigned audio_count;
static struct { unsigned kind; uint32_t tag, elapsed; } audio_events[32];
uint32_t device_card_tag(void) { return current_tag; }
void handset_audio_event(uint8_t kind,uint32_t tag,uint32_t elapsed) {
    REQUIRE(audio_count < 32);
    audio_events[audio_count].kind=kind;audio_events[audio_count].tag=tag;
    audio_events[audio_count++].elapsed=elapsed;
}
static unsigned stats_count;
void handset_audio_stats(unsigned received_ms,unsigned played_ms,unsigned dry,unsigned dry_ms,
                         unsigned underruns,unsigned starved_ms,unsigned worst_gap_ms){
    (void)received_ms;(void)dry;(void)dry_ms;(void)underruns;(void)starved_ms;(void)worst_gap_ms;
    REQUIRE(played_ms<=30000);stats_count++;
}
static int16_t recorded_output[CAP+4096];
static int volume_at[5], level_at[5], current_level;

QueueHandle_t xQueueCreate(unsigned cap, unsigned width) {
    REQUIRE(cap<=12 && width<=32);queue=(struct HostQueue){.capacity=cap,.width=width};return &queue;
}
int xQueueSend(QueueHandle_t q,const void *msg,unsigned ignored) {
    REQUIRE(q==&queue);
    if(q->count==q->capacity)return 0;
    memcpy(q->bytes[(q->first+q->count)%q->capacity],msg,q->width);++q->count;return 1;
}
int xQueueSendToFront(QueueHandle_t q,const void *msg,unsigned ignored) {
    REQUIRE(q==&queue);
    if(q->count==q->capacity)return 0;
    q->first=(q->first+q->capacity-1)%q->capacity;memcpy(q->bytes[q->first],msg,q->width);++q->count;return 1;
}
int xQueuePeek(QueueHandle_t q,void *out,unsigned ignored) {
    REQUIRE(q==&queue);if(!q->count)return 0;memcpy(out,q->bytes[q->first],q->width);return 1;
}
int xQueueReceive(QueueHandle_t q,void *out,unsigned wait) {
    if(!xQueuePeek(q,out,0)){
        // At this point the real worker blocks in its top-level dispatch loop.
        if(wait==portMAX_DELAY){REQUIRE(in_worker && !sem.held);longjmp(idle,1);}
        return 0;
    }
    q->first=(q->first+1)%q->capacity;--q->count;return 1;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &sem;}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return &done_sem;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned wait){
    if(h->binary){
        REQUIRE(wait==portMAX_DELAY);
        pthread_mutex_lock(&lk);while(!h->count)pthread_cond_wait(&cv,&lk);h->count=0;pthread_mutex_unlock(&lk);return 1;
    }
    REQUIRE(!h->held);h->held=true;return 1;
}
int xSemaphoreGive(SemaphoreHandle_t h){
    if(h->binary){pthread_mutex_lock(&lk);h->count=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lk);return 1;}
    REQUIRE(h->held);h->held=false;return 1;
}
static void *thread_entry(void *entry){((void(*)(void*))entry)(NULL);return NULL;}
int xTaskCreate(void(*entry)(void*),const char *name,unsigned bytes,void *arg,unsigned pri,void *out){
    if(!strcmp(name,"dot_uplink")){
        pthread_t t;REQUIRE(!pthread_create(&t,NULL,thread_entry,(void*)entry));
        *(TaskHandle_t *)out=&uplink_host;return 1;
    }
    worker=entry;worker_arg=arg;return 1;
}
int xTaskNotifyGive(TaskHandle_t t){
    REQUIRE(t==&uplink_host);pthread_mutex_lock(&lk);++notified;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lk);return 1;
}
uint32_t ulTaskNotifyTake(int clear,unsigned ticks){
    pthread_mutex_lock(&lk);
    if(!notified){
        if(ticks==portMAX_DELAY)while(!notified)pthread_cond_wait(&cv,&lk);
        else{struct timespec at;clock_gettime(CLOCK_REALTIME,&at);at.tv_nsec+=1000000;if(at.tv_nsec>=1000000000){at.tv_sec++;at.tv_nsec-=1000000000;}
             pthread_cond_timedwait(&cv,&lk,&at);}
    }
    uint32_t r=notified;notified=clear?0:(notified?notified-1:0);pthread_mutex_unlock(&lk);return r;
}
static void run_worker(void){
    REQUIRE(worker && !in_worker);
    in_worker=true;
    if(setjmp(idle)==0){worker(worker_arg);REQUIRE(false);}
    in_worker=false;
}
void vTaskDelay(unsigned ticks){
    ms+=ticks;REQUIRE(!sem.held);if(on_delay)on_delay();
}
int64_t esp_timer_get_time(void){return (int64_t)ms*1000;}
void log_audio(const char *tag,const char *format,...){(void)tag;(void)format;}
static atomic_uint random_seq = 0x12345678;
uint32_t esp_random(void){return atomic_fetch_add(&random_seq, 0x9e3779b9u);}
void *heap_caps_malloc(size_t bytes,int caps){return calloc(1,bytes);}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void){return &mic;}
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void){return &speaker;}
int esp_codec_dev_open(esp_codec_dev_handle_t c,esp_codec_dev_sample_info_t *fmt){
    REQUIRE(fmt->sample_rate==16000 && fmt->bits_per_sample==16 && fmt->channel==1);REQUIRE(!c->opened);c->opened=true;if(c==&speaker && on_open)on_open();return 0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t c){REQUIRE(c->opened);c->opened=false;return 0;}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t c,float gain){REQUIRE(c==&mic && c->opened);return 0;}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t c,int vol){REQUIRE(c==&speaker && c->opened);c->volume=vol;return 0;}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t c,bool yes){REQUIRE(c==&speaker && c->opened);c->muted=yes;return 0;}
int esp_codec_dev_read(esp_codec_dev_handle_t c,void *pcm,int n){
    REQUIRE(c==&mic && c->opened && n==BLOCK*2);ms+=20;++mic_reads;
    int16_t *out=pcm;for(int i=0;i<n/2;i++)out[i]=0;if(on_read)on_read();return 0;
}
int esp_codec_dev_write(esp_codec_dev_handle_t c,void *pcm,int n){
    REQUIRE(c==&speaker && c->opened && n>0 && n<=BLOCK*2 && n%2==0 && !sem.held);
    REQUIRE(samples+(unsigned)n/2<=sizeof(recorded_output)/sizeof(*recorded_output));
    if(outputs==0)first_before_end=!end_sent;
    if(outputs<5){volume_at[outputs]=speaker.muted?-1:speaker.volume;level_at[outputs]=current_level;}
    memcpy(recorded_output+samples,pcm,(size_t)n);samples+=(unsigned)n/2;outputs++;ms+=(unsigned)n/32;
    if(on_write)on_write();
    return 0;
}
int handset_payload(void){return 244;}
bool handset_streams_mic(void){return phone_streams;}
bool handset_stream(const uint8_t *p,int n){REQUIRE(p[0]==0x11 && live_open);return handset_send(p,n);}
bool handset_send(const uint8_t *p,int n){
    REQUIRE(n<=244);
    // 0.4 live microphone: 13 id | 11 seq data | 14 id total ms, or 16 id.
    if(p[0]==0x13){REQUIRE(n==5 && !live_open && !xfer_open);memcpy(&live_id,p+1,4);live_open=true;live_started=true;return true;}
    if(live_open){
        uint32_t id;
        if(p[0]==0x11){
            REQUIRE(n>=4 && (unsigned)(p[1]+256*p[2])==(live_seq&0xffff));live_seq++;
            for(int i=3;i<n;i++)REQUIRE(p[i]==0xff);live_bytes+=(unsigned)n-3;return true;
        }
        if(p[0]==0x14){
            uint32_t total;REQUIRE(n==11);memcpy(&id,p+1,4);memcpy(&total,p+5,4);
            REQUIRE(id==live_id && total==live_bytes);live_tail=p[9]|p[10]<<8;live_done=true;live_open=false;return true;
        }
        REQUIRE(p[0]==0x16 && n==5);memcpy(&id,p+1,4);REQUIRE(id==live_id);live_dropped=true;live_open=false;return true;
    }
    if(p[0]==0x10){REQUIRE(n==9 && !xfer_open);memcpy(&xfer_size,p+1,4);memcpy(&xfer_id,p+5,4);xfer_open=true;return true;}
    REQUIRE(xfer_open);
    if(p[0]==0x11){
        REQUIRE(n>=4 && (unsigned)(p[1]+256*p[2])==xfer_seq++);
        for(int i=3;i<n;i++)REQUIRE(p[i]==0xff);xfer_bytes+=(unsigned)n-3;return true;
    }
    uint32_t id;REQUIRE(p[0]==0x12 && n==5);memcpy(&id,p+1,4);REQUIRE(id==xfer_id && xfer_bytes==xfer_size);
    xfer_done=true;xfer_open=false;return true;
}
void device_reply(const char *s,int k){++errors;fprintf(stderr,"firmware: %s\n",s);}
void device_level(int level){REQUIRE(level>=0);}
void device_speech_level(int level){current_level=level;}
static bool op(unsigned tag){
    uint8_t b=(uint8_t)tag;bool ok=voice_speaker_packet(&b,1);
    if(ok&&(tag==0x45||tag==0x42))end_sent=true;return ok;
}
static bool legacy_begin(uint32_t bytes){
    uint8_t b[5]={0x40};memcpy(b+1,&bytes,4);return voice_speaker_packet(b,5);
}
static bool data(unsigned tag,unsigned count,uint8_t sample){
    uint8_t packet[1+CHUNK];packet[0]=(uint8_t)tag;memset(packet+1,sample,CHUNK);
    while(count){unsigned n=count<CHUNK?count:CHUNK;if(!voice_speaker_packet(packet,(int)n+1))return false;count-=n;}return true;
}
static void reset(void){
    REQUIRE(!mic.opened && !speaker.opened && !queue.count);
    REQUIRE(op(0x46));audio_count=0;current_tag++;voice_set_volume(58);samples=outputs=errors=0;current_level=0;end_sent=first_before_end=false;
    on_open=on_delay=on_write=NULL;ms+=100;memset(volume_at,0,sizeof(volume_at));memset(level_at,0,sizeof(level_at));
}
static void values(unsigned from,unsigned to,int16_t expected){
    REQUIRE(from<=to && to<=samples);
    for(unsigned i=from;i<to;i++)REQUIRE(recorded_output[i]==expected);
}
static void finish_stream(void){
    on_delay=NULL;REQUIRE(samples==3200 && !end_sent);
    REQUIRE(data(0x44,831,0x00));REQUIRE(op(0x45));
}
static void stream_first(void){
    reset();REQUIRE(op(0x43));REQUIRE(data(0x44,3000,0x80));run_worker();REQUIRE(samples==0);
    REQUIRE(data(0x44,200,0x80));on_delay=finish_stream;run_worker();
    REQUIRE(first_before_end && samples==4031 && errors==0);
    values(0,3200,32124);values(3200,4031,-32124);puts("PASS stream starts at 3,200 samples before END; 831 post-start samples delivered in order");
}
static void legacy_and_replay(void){
    reset();REQUIRE(legacy_begin(1050));REQUIRE(data(0x41,800,0x80));REQUIRE(!op(0x42));run_worker();REQUIRE(!samples);
    REQUIRE(data(0x41,250,0x00));run_worker();REQUIRE(!samples);REQUIRE(op(0x42));run_worker();
    REQUIRE(!first_before_end && samples==1050);values(0,800,32124);values(800,1050,-32124);
    voice_action(PLAY_LAST);run_worker();REQUIRE(samples==2100);values(1050,1850,32124);values(1850,2100,-32124);
    puts("PASS legacy output waits for exact declared length plus END; replay is byte-for-byte equivalent");
}
static void replace_cancelled(void){
    on_open=on_write=NULL;REQUIRE(op(0x46));REQUIRE(!data(0x44,20,0xff));REQUIRE(!op(0x45));
    REQUIRE(op(0x43));REQUIRE(data(0x44,105,0x00));REQUIRE(op(0x45));
}
static void replace_directly(void){
    on_write=NULL;REQUIRE(op(0x43));REQUIRE(data(0x44,105,0x00));REQUIRE(op(0x45));
}
static void cancel_replace(void){
    for(unsigned i=0;i<2;i++){
        reset();REQUIRE(op(0x43));REQUIRE(data(0x44,6400,0x80));
        on_write=i?replace_directly:replace_cancelled;run_worker();
        REQUIRE(samples==BLOCK+105);values(0,BLOCK,32124);values(BLOCK,BLOCK+105,-32124);
    }
    puts("PASS cancellation and implicit replacement stop old clip after the block already playing; no stale samples in the new clip");
}
static void volume_hook(void){
    if(outputs==1)voice_set_volume(0);
    if(outputs==2)voice_set_volume(67);
}
static void mute_volume(void){
    reset();voice_set_volume(23);on_write=volume_hook;
    REQUIRE(legacy_begin(960));REQUIRE(data(0x41,960,0x80));REQUIRE(op(0x42));run_worker();
    REQUIRE(outputs==3 && volume_at[0]==23 && volume_at[1]==-1 && volume_at[2]==67);
    REQUIRE(level_at[0]>0 && level_at[1]==0 && level_at[2]>0);
    puts("PASS volume, hardware mute/unmute and mouth-level tracking update on successive 20 ms blocks");
}
static void limits(void){
    reset();REQUIRE(!legacy_begin(CAP+1));REQUIRE(!legacy_begin(0));REQUIRE(legacy_begin(CAP));
    REQUIRE(data(0x41,CAP,0xfe));REQUIRE(!data(0x41,1,0xfe));REQUIRE(op(0x42));run_worker();
    REQUIRE(samples==CAP);values(0,CAP,8);
    reset();REQUIRE(op(0x43));REQUIRE(data(0x44,CAP,0x80));REQUIRE(!data(0x44,1,0x80));REQUIRE(op(0x45));run_worker();
    REQUIRE(samples==CAP);values(0,CAP,32124);
    reset();mic_reads=xfer_bytes=xfer_seq=xfer_size=0;xfer_open=xfer_done=false;
    voice_action(MIC_START);run_worker();
    REQUIRE(mic_reads==1503 && xfer_done && !xfer_open && xfer_bytes==CAP && !errors);
    puts("PASS 480,000 samples accepted (legacy and streaming), extra sample rejected; auto-stop captures all 30 s and sends consecutive GATT packets");
}
static bool stalled(void){
    reset();REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));run_worker();
    REQUIRE(samples==3200); // audio worker yielded after its underrun deadline
    bool accepted=data(0x44,320,0x00);bool finished=op(0x45);run_worker();
    if(accepted && finished && samples==3200) {
        puts("FAIL >15 s underrun: sender receives success for the tail and END but the 320 tail samples never play");
        return false;
    }
    puts("PASS >15 s underrun: closed/rejected or tail actually played");return true;
}
static bool cancel_during_codec_open(void){
    reset();REQUIRE(op(0x43));REQUIRE(data(0x44,6400,0x80));
    // Hardware open can block while a GATT write on another task replaces the
    // stream. The old buffer has been copied but never submitted to the codec.
    on_open=replace_cancelled;
    run_worker();
    if(samples==BLOCK+105) {
        values(0,BLOCK,32124);values(BLOCK,BLOCK+105,-32124);
        puts("FAIL cancel during codec open: first 20 ms of cancelled clip are newly submitted after cancellation");
        return false;
    }
    REQUIRE(samples==105);values(0,105,-32124);
    puts("PASS cancel during codec open: only the new clip reaches the speaker");return true;
}
static void bind_response(uint32_t id) {
    uint8_t bind[9]={0x47};memcpy(bind+1,&current_tag,4);memcpy(bind+5,&id,4);
    REQUIRE(voice_speaker_packet(bind,sizeof(bind)));
}
static void correlated_receipts(void) {
    reset();mic_reads=xfer_bytes=xfer_seq=xfer_size=0;xfer_open=xfer_done=false;
    voice_action(MIC_START);run_worker();REQUIRE(xfer_done);
    ms+=264000;bind_response(xfer_id);
    REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));REQUIRE(op(0x45));run_worker();
    REQUIRE(audio_count==2 && audio_events[0].kind==1 && audio_events[1].kind==2);
    REQUIRE(audio_events[0].tag==current_tag && audio_events[1].tag==current_tag);
    REQUIRE(audio_events[0].elapsed>=264000 && audio_events[0].elapsed<264100);
    REQUIRE(audio_events[1].elapsed>=audio_events[0].elapsed && audio_events[1].elapsed>264100);
    voice_action(PLAY_LAST);run_worker();
    REQUIRE(audio_count==4 && audio_events[2].elapsed==0 && audio_events[3].kind==2 && audio_events[3].elapsed==0);
    reset();bind_response(0xabcdef01);
    REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));REQUIRE(op(0x45));run_worker();
    REQUIRE(audio_count==2 && audio_events[0].elapsed==0);
    reset();REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));run_worker();
    REQUIRE(audio_count==2 && audio_events[1].kind==3);
    puts("PASS codec start/end/cancel receipts, >180 s recording binding, replay and unrelated-response exclusion");
}
static int rebuffer_phase, rebuffer_waits;
static void rebuffer_hook(void){
    if(rebuffer_phase==0){REQUIRE(samples==3200);REQUIRE(data(0x44,8000,0x00));rebuffer_phase=1;return;}
    if(rebuffer_phase==1){
        REQUIRE(samples==3200); // 8,000 < 24,000: still refilling after the underrun
        if(++rebuffer_waits==5){REQUIRE(data(0x44,16000,0x00));rebuffer_phase=2;}
        return;
    }
    if(rebuffer_phase==2){REQUIRE(samples==27200);REQUIRE(op(0x45));rebuffer_phase=3;}
}
static void rebuffer_after_underrun(void){
    // 0.5.4: after one underrun the board waits for 1.5 s of audio (0.5 s in
    // 0.4.4), so a slow stretch is one pause instead of a series of short cuts.
    reset();rebuffer_phase=rebuffer_waits=0;
    REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));on_delay=rebuffer_hook;run_worker();on_delay=NULL;
    REQUIRE(rebuffer_phase==3 && samples==27200 && errors==0);
    values(0,3200,32124);values(3200,27200,-32124);
    puts("PASS 0.5.4 underrun: playback resumes only after 24,000 samples, then plays to END in order");
}
static int slow_phase;
static void slow_hook(void){
    if(slow_phase==0){REQUIRE(samples==0);REQUIRE(data(0x44,12800,0x00));slow_phase=1;return;}
    if(slow_phase==1){REQUIRE(samples==16000);REQUIRE(op(0x45));slow_phase=2;}
}
static void slow_start(void){
    // 0.5.4: 3,200 samples that took 400 ms to arrive (8 KB/s, below 1.25x real
    // time) do not start playback; it starts with 1 s buffered.
    reset();slow_phase=0;REQUIRE(op(0x43));ms+=400;REQUIRE(data(0x44,3200,0x80));
    on_delay=slow_hook;run_worker();on_delay=NULL;
    REQUIRE(slow_phase==2 && samples==16000 && errors==0);
    values(0,3200,32124);values(3200,16000,-32124);
    puts("PASS 0.5.4 slow link: playback waits for 1 s of audio, then plays to END in order");
}
static unsigned start_at, wait_at, send_at, discard_at, disarm_at, reply_at;
static void wait_live(void){
    for(int i=0;i<3000 && !live_bytes;i++)usleep(1000);
    REQUIRE(live_bytes>0);
}
static void scripted(void){
    if(mic_reads==start_at)voice_action(MIC_START);
    if(mic_reads==wait_at)wait_live();
    if(mic_reads==send_at)voice_action(MIC_SEND);
    if(mic_reads==discard_at)voice_action(MIC_DISCARD);
    if(mic_reads==disarm_at)voice_action(MIC_DISARM);
    if(mic_reads==reply_at){REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));REQUIRE(op(0x45));}
}
static void live_reset(bool streams){
    reset();mic_reads=0;phone_streams=streams;
    live_open=live_done=live_dropped=live_started=false;live_bytes=live_seq=live_tail=0;
    xfer_bytes=xfer_seq=xfer_size=0;xfer_open=xfer_done=false;
    start_at=wait_at=send_at=discard_at=disarm_at=reply_at=0;on_read=scripted;
}
static void live_hold(void){
    // Touch-down arms, 260 ms later the hold confirms, release after 2 s.
    live_reset(true);start_at=13;wait_at=60;send_at=103;
    voice_action(MIC_ARM);run_worker();
    REQUIRE(live_done && !live_open && !live_dropped && !xfer_done && !errors);
    REQUIRE(live_bytes==100*BLOCK && live_seq==(100*BLOCK+240)/241 && live_tail<1000);
    REQUIRE(!mic.opened);
    puts("PASS 0.4 hold-to-talk: armed at touch, audio reaches the phone before release, END carries total and tail");
}
static void live_tap(void){
    live_reset(true);disarm_at=5;
    voice_action(MIC_ARM);run_worker();
    REQUIRE(!live_started && !xfer_open && !errors && !mic.opened && mic_reads==5);
    puts("PASS 0.4 short tap: armed microphone is dropped silently, nothing sent");
}
static void live_discard(void){
    live_reset(true);start_at=13;wait_at=60;discard_at=80;
    voice_action(MIC_ARM);run_worker();
    REQUIRE(live_started && live_dropped && !live_done && !live_open && !errors);
    puts("PASS 0.4 discard while streaming: phone receives 16 with the same id and no END");
}
static void live_reply_during_arm(void){
    live_reset(true);reply_at=5;
    voice_action(MIC_ARM);run_worker();
    REQUIRE(!live_started && !errors && samples==3200);values(0,3200,32124);
    puts("PASS 0.4 reply arriving before the hold confirms: arm dropped, reply plays in full");
}
static void live_too_short(void){
    live_reset(true);send_at=10;
    voice_action(MIC_START);run_worker();
    REQUIRE(!live_started && !xfer_open && errors==1);errors=0;
    puts("PASS 0.4 clip under 0.25 s: never streamed, user sees the short-clip message");
}
static void live_thirty(void){
    live_reset(true);
    voice_action(MIC_START);run_worker();
    REQUIRE(mic_reads==1503 && live_done && live_bytes==CAP && live_seq==(CAP+240)/241 && !errors);
    puts("PASS 0.4 30 s cap: 480,000 bytes streamed in 1,992 consecutive notifications, END total matches");
}
static void live_old_phone(void){
    live_reset(false);start_at=13;send_at=103;
    voice_action(MIC_ARM);run_worker();
    REQUIRE(!live_started && xfer_done && xfer_bytes==100*BLOCK && !errors);
    puts("PASS 0.4 phone without 31 01: same pre-roll, legacy 10/11/12 after release");
}
static void live_bound_reply(void){
    // 0.4.2: a reply bound (47) to a recording that was streamed while it was
    // recorded must still get the release-to-first-codec measure.
    live_reset(true);start_at=13;wait_at=60;send_at=103;audio_count=0;
    voice_action(MIC_ARM);run_worker();REQUIRE(live_done);
    ms+=5000;bind_response(live_id);
    REQUIRE(op(0x43));REQUIRE(data(0x44,3200,0x80));REQUIRE(op(0x45));run_worker();
    REQUIRE(audio_count==2 && audio_events[0].kind==1 && audio_events[1].kind==2);
    REQUIRE(audio_events[0].tag==current_tag && audio_events[0].elapsed>=5000 && audio_events[0].elapsed<5100);
    puts("PASS 0.4.2 streamed recording keeps the 47 binding: release-to-codec measured for its reply");
}
int main(int argc,char **argv){
    voice_init();
    stream_first();legacy_and_replay();cancel_replace();mute_volume();limits();
    bool stall_ok=stalled(), cancel_ok=cancel_during_codec_open();correlated_receipts();
    on_read=NULL;
    rebuffer_after_underrun();slow_start();
    live_hold();live_tap();live_discard();live_reply_during_arm();live_too_short();live_thirty();live_old_phone();live_bound_reply();
    return stall_ok&&cancel_ok?0:1;
}
