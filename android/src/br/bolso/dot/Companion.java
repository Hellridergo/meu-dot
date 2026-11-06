package br.bolso.dot;
import android.app.*;
import android.content.*;
import android.os.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

public final class Companion extends Service {
  static volatile String status = "Desconectado. Ative a placa para parear.";
  final Handler h = new Handler(Looper.getMainLooper());
  final ExecutorService net = Executors.newSingleThreadExecutor(),
                        sttIO = Executors.newSingleThreadExecutor();
  final ExecutorService telemetryIO = new ThreadPoolExecutor(1,1,0L,TimeUnit.MILLISECONDS,
      new ArrayBlockingQueue<Runnable>(4),new ThreadPoolExecutor.DiscardOldestPolicy());
  volatile boolean telemetrySupported;
  static volatile Timings timing = new Timings();
  static final BatteryInfo battery = new BatteryInfo();
  static volatile boolean linked, bridgeReady;
  static volatile long lastPollMs;
  final LinkedHashMap<String, Timings> requests = new LinkedHashMap<>();
  Timings activeTrace, latestInput;
  NoticeReceipt notice;
  boolean receipts;
  String receiptedId = "";
  Radio radio;
  volatile boolean closed;
  VoiceOutput output;
  boolean warmRequested;
  long awaiting;
  PowerManager.WakeLock awake;
  static long clock() { return SystemClock.elapsedRealtime(); }
  boolean cloudInput() {
    SharedPreferences v = getSharedPreferences("voice", 0);
    return v.getBoolean("consent", false) &&
        "google".equals(v.getString("stt", "local"));
  }
  final SharedPreferences.OnSharedPreferenceChangeListener onVoiceChange =
      (p, k) -> {
    if (output != null)
      output.cancel();
    if (cloudInput())
      cool();
    if (!closed && radio != null && radio.active)
      poll();
  };
  final SharedPreferences.OnSharedPreferenceChangeListener onConfigChange = (p,k) -> {
    if ("sound".equals(k) && !p.getBoolean("sound", true)) {
      if (output != null) output.cancel();
      receiptIfReady();
    }
    if ("url".equals(k)) {
      this.generation++;
      activeId = "";
      notice = null;
      activeTrace = null;
      available = null;
      receipts = false;
      telemetrySupported = false;
      receiptedId = "";
      p.edit().remove("last_receipted_id").apply();
      if (output != null) output.cancel();
      scheduleSoon();
    }
  };
  void cool() {
    if (warmRequested) {
      warmRequested = false;
      sttIO.execute(() -> {
        try {
          NativeVoice.release();
        } catch (UnsatisfiedLinkError ignored) {
        }
      });
    }
  }
  void prewarm() {
    if (!warmRequested && OfflineVoice.ready(this) && !cloudInput()) {
      warmRequested = true;
      sttIO.execute(() -> {
        try {
          OfflineVoice.warm(this);
        } catch (IOException | UnsatisfiedLinkError ignored) {
        }
      });
    }
  }
  void releaseWake() {
    if (awake != null && awake.isHeld())
      awake.release();
  }
  /** The board began streaming a recording: stop the previous reply now. */
  void receiptStarting() {
    if (output != null)
      output.cancel();
    awaiting = 0;
    if (awake != null)
      awake.acquire(360000);
  }
  Timings beginReceipt(int clipId) { return beginReceipt(clipId, clock()); }
  /** at: when the user released, on this phone's clock. */
  Timings beginReceipt(int clipId, long at) {
    Timings trace = new Timings(UUID.randomUUID().toString(), clipId, at);
    latestInput = timing = trace;
    requests.put(trace.recordingId, trace);
    while (requests.size() > 16) requests.remove(requests.keySet().iterator().next());
    receiptStarting();
    return trace;
  }
  boolean trace(Timings t, int stage) {
    if (t != null && t.mark(stage, clock())) {
      android.util.Log.i("MeuDotTiming", "recording=" + t.recordingId + " request=" + t.requestId + " stage=" + stage);
      pref().edit().putString("last_trace", t.summary())
          .putString("last_trace_json", new JSONObject(t.export()).toString()).apply();
      return true;
    }
    return false;
  }
  void telemetry(Timings t,String trigger) {
    if(t==null||t.requestId.isEmpty()||!telemetrySupported||closed)return;
    final String base=activeBase;
    final long version=generation;
    final byte[] bytes;
    try{
      bytes=new JSONObject().put("recording_id",t.recordingId).put("request_id",t.requestId)
          .put("trigger",trigger).put("metrics",new JSONObject(t.metrics())).toString().getBytes(StandardCharsets.UTF_8);
    }catch(JSONException e){return;}
    // Bounded best-effort diagnostics have their own executor and short timeout.
    // They cannot delay speech, delivery receipts or a pending explicit choice.
    telemetryIO.execute(()->{
      if(closed||generation!=version)return;
      try{api(base,"POST","/telemetry",bytes,"application/json");}
      catch(Exception e){android.util.Log.w("MeuDotTiming","Telemetry unavailable; local trace retained");}
    });
  }
  void cardDisplayed(int tag) {
    if (notice != null && tag == notice.tag) {
      notice.displayed = true;
      receiptIfReady();
    }
  }
  void audioEvent(int kind, int tag, long elapsed) {
    if (tag != activeTag) return;
    if (kind == 1 && activeTrace != null) {
      activeTrace.board(elapsed);
      if(trace(activeTrace, Timings.SPEAKER))telemetry(activeTrace,"first_audio");
    } else if (kind == 2) {
      if(activeTrace!=null)activeTrace.boardDone(elapsed);
      if(trace(activeTrace, Timings.PLAYED))telemetry(activeTrace,"playback_end");
      releaseWake();
      if (notice != null && tag == notice.tag) {
        notice.played = true;
        receiptIfReady();
      }
    } else if (kind == 3) {
      telemetry(activeTrace,"cancelled");
      releaseWake();
      report("Leitura interrompida. A mensagem continua na tela da placa.");
    }
  }
  void receiptIfReady() {
    if (!receipts || notice == null || notice.id.startsWith("@")) return;
    String stage = notice.stage(pref().getBoolean("sound", true));
    if (stage == null || notice.id.equals(receiptedId)) return;
    if (!pref().getString("ack_id", "").isEmpty()) return;
    // Persist before advancing. A restart or lost HTTP response retries the
    // same receipt; it never invents yes/no/dismiss.
    if (pref().edit().putString("receipt_id", notice.id)
        .putString("receipt_stage", stage).putString("last_receipted_id", notice.id).commit()) {
      receiptedId = notice.id;
      scheduleSoon();
    }
  }
  void scheduleSoon() {
    if (closed)
      return;
    if (polling) { pollAgain = true; return; }
    h.removeCallbacks(pulse);
    h.post(pulse);
  }
  boolean polling, pollAgain, testAfterLink;
  Boolean available;
  int activeTag;
  String activeId = "";
  volatile long generation;
  String activeBase = "";
  boolean localInput;
  static final class HttpError extends IOException {
    final int status;
    HttpError(int status) {
      super("HTTP " + status);
      this.status = status;
    }
  }
  SharedPreferences pref() { return getSharedPreferences("config", 0); }
  public IBinder onBind(Intent i) { return null; }
  Notification notification() {
    return new Notification.Builder(this, "device")
        .setContentTitle("Meu Dot")
        .setContentText(status)
        .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
        .setOnlyAlertOnce(true)
        .setOngoing(true)
        .setContentIntent(
            PendingIntent.getActivity(this, 0, new Intent(this, Main.class),
                                      PendingIntent.FLAG_IMMUTABLE))
        .build();
  }
  void report(String s) {
    if (s.equals(status))
      return;
    status = s;
    if (!closed)
      getSystemService(NotificationManager.class).notify(8, notification());
  }
  public void onCreate() {
    super.onCreate();
    getSystemService(NotificationManager.class)
        .createNotificationChannel(
            new NotificationChannel("device", "Conexão com a Waveshare",
                                    NotificationManager.IMPORTANCE_LOW));
    startForeground(8, notification());
    radio = new Radio(this);
    awake = getSystemService(PowerManager.class)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DotDeBolso:voz");
    awake.setReferenceCounted(false);
    output = new VoiceOutput(this);
    receiptedId = pref().getString("last_receipted_id", "");
    getSharedPreferences("voice", 0)
        .registerOnSharedPreferenceChangeListener(onVoiceChange);
    pref().registerOnSharedPreferenceChangeListener(onConfigChange);
    h.post(pulse);
  }
  public int onStartCommand(Intent i, int flags, int id) {
    if (i != null && "test".equals(i.getAction())) {
      if (radio.active)
        test();
      else {
        testAfterLink = true;
        radio.connect();
      }
    } else if (radio.active) {
      generation++;
      available = null;
      activeBase = "";
      activeId = "";
      notice = null;
      activeTrace = null;
      scheduleSoon();
    } else
      radio.connect();
    return START_NOT_STICKY;
  }
  public void onDestroy() {
    closed = true;
    h.removeCallbacksAndMessages(null);
    getSharedPreferences("voice", 0)
        .unregisterOnSharedPreferenceChangeListener(onVoiceChange);
    pref().unregisterOnSharedPreferenceChangeListener(onConfigChange);
    output.close();
    radio.close();
    net.shutdownNow();
    telemetryIO.shutdownNow();
    cool();
    sttIO.shutdown();
    releaseWake();
    super.onDestroy();
  }
  final Runnable pulse = new Runnable() {
    public void run() {
      if (closed)
        return;
      poll();
      long delay = PollSchedule.delay(-1);
      if (awaiting != 0) {
        long age = clock() - awaiting;
        delay = PollSchedule.delay(age);
        if (age >= 300000) {
          // Stop the fast schedule, not the request trace. Late replies keep
          // their recording ID and remain measurable.
          releaseWake();
        }
      }
      h.postDelayed(this, delay);
    }
  };
  void connected() {
    linked = true;
    telemetrySupported = false;
    generation++;
    available = null;
    activeId = "";
    report("Placa pareada. Verificando a ponte…");
    if (testAfterLink) {
      testAfterLink = false;
      test();
    }
    poll();
  }
  void disconnected(String msg) {
    linked = bridgeReady = false;
    telemetrySupported = false;
    battery.clear();
    notice = null;
    activeTrace = null;
    generation++;
    available = null;
    activeId = "";
    if (output != null)
      output.cancel();
    awaiting = 0;
    releaseWake();
    report(msg);
  }
  void test() {
    if (!radio.active)
      return;
    if (!activeId.isEmpty() && !activeId.equals("@teste") && !activeId.equals(receiptedId)) {
      report("Conclua a mensagem atual antes do teste.");
      return;
    }
    String s = "Olá! A tela está conectada ao seu Android.";
    activeId = "@teste";
    activeTag = new Random().nextInt();
    activeTrace = null;
    notice = new NoticeReceipt(activeId, activeTag, false);
    radio.card(0, activeTag, s);
    output.speak(s, true);
    report("Teste local enviado à Waveshare.");
  }
  void fail(String msg) {
    report(msg);
    awaiting = 0;
    releaseWake();
    if (radio.active)
      radio.card(2, 0, msg);
  }
  void remember(String id, String text, boolean decision) {
    try {
      JSONArray previous = new JSONArray(pref().getString("history", "[]"));
      for (int i = 0; i < previous.length(); i++)
        if (id.equals(previous.getJSONObject(i).optString("id"))) return;
      JSONArray history = new JSONArray();
      for (int i = Math.max(0, previous.length() - 19); i < previous.length(); i++)
        history.put(previous.getJSONObject(i));
      history.put(new JSONObject().put("id", id).put("text", text)
          .put("decision", decision).put("at", System.currentTimeMillis()));
      pref().edit().putString("history", history.toString()).putString("last_message", text).apply();
    } catch (JSONException ignored) {
      pref().edit().putString("last_message", text).apply();
    }
  }
  JSONObject api(String base, String method, String path, byte[] data,
                 String contentType) throws Exception {
    if (!base.startsWith("https://"))
      throw new IOException("Configure a ponte.");
    HttpURLConnection c =
        (HttpURLConnection) new URL(base + path).openConnection();
    try {
      c.setConnectTimeout(path.equals("/telemetry")?2000:8000);
      c.setReadTimeout(path.equals("/telemetry")?2000:path.equals("/state") || path.equals("/receipt") || path.equals("/choice") ? 10000 : 90000);
      c.setInstanceFollowRedirects(false);
      c.setRequestMethod(method);
      c.setRequestProperty("X-Dot-Client", "android-4");
      c.setRequestProperty("Authorization",
                           "Bearer " + SecureConfig.tokenFor(this, base));
      if (data != null) {
        c.setRequestProperty("Content-Type", contentType);
        c.setDoOutput(true);
        c.setFixedLengthStreamingMode(data.length);
        try (OutputStream os = c.getOutputStream()) {
          os.write(data);
        }
      }
      int status = c.getResponseCode();
      if (status < 200 || status > 299)
        throw new HttpError(status);
      try (InputStream in = c.getInputStream();
           ByteArrayOutputStream buffer = new ByteArrayOutputStream()) {
        byte[] part = new byte[2048];
        int n;
        while ((n = in.read(part)) != -1) {
          if (buffer.size() + n > 65536)
            throw new IOException("Retorno muito longo.");
          buffer.write(part, 0, n);
        }
        return new JSONObject(buffer.toString("UTF-8"));
      }
    } finally {
      c.disconnect();
    }
  }
  void setOnline(boolean yes) {
    bridgeReady = yes;
    if (radio.active && (available == null || available != yes)) {
      radio.send(new byte[] {1, (byte)(yes ? 1 : 0)});
      available = yes;
    }
  }
  void upload(byte[] audio, Timings trace) {
    if (!radio.active)
      return;
    if (!Boolean.TRUE.equals(available)) {
      fail("A ponte do Dot está indisponível.");
      return;
    }
    final String id = trace.recordingId;
    final long version = generation;
    final String base = activeBase;
    final boolean localProtocol = localInput || OfflineVoice.siteAddress(base);
    final boolean google = cloudInput();
    trace.input = google ? "Google" : localProtocol ? "Whisper local" : "ponte";
    trace(trace, Timings.AUDIO);
    report(google          ? "Transcrevendo com o Google…"
           : localProtocol ? "Transcrevendo no celular…"
                           : "Transcrevendo na ponte…");
    sttIO.execute(() -> {
      try {
        trace(trace,Timings.STT_START);
        if (localProtocol) {
          String text;
          try {
            text = google ? CloudVoice.transcribe(
                                SecureConfig.provider(this, "google"), audio)
                          : OfflineVoice.transcribe(this, audio);
          } catch (Exception ex) {
            String m =
                "Falha na transcrição local. Confira o modelo do Android.";
            if (google)
              m = ex.getClass() == IOException.class &&
                          ex.getMessage() != null &&
                          ex.getMessage().startsWith("Google (")
                      ? ex.getMessage()
                      : "Gemini não concluiu. Confira a chave, o acesso ao "
                            + "modelo e a internet.";
            final String safe = m;
            h.post(() -> {
              if (!closed && version == generation)
                fail(safe);
            });
            return;
          }
          trace(trace, Timings.TEXT);
          if (text.isEmpty()) {
            h.post(() -> {
              if (!closed && version == generation)
                fail("Não ouvi uma frase clara. Tente novamente.");
            });
            return;
          }
          if (closed || version != generation)
            return;
          trace(trace,Timings.UPLOAD_START);
          JSONObject transcript =
              new JSONObject()
                  .put("text", text)
                  .put("audio_seconds", audio.length / 16000.0)
                  .put("client_metrics",new JSONObject(trace.metrics()));
          api(base, "POST", "/transcript/" + id,
              transcript.toString().getBytes(StandardCharsets.UTF_8),
              "application/json");
        } else {
          trace(trace,Timings.UPLOAD_START);
          api(base, "POST", "/voice/" + id, audio, "application/octet-stream");
        }
        trace(trace, Timings.SENT);
        h.post(() -> {
          if (!closed && version == generation) {
            awaiting = activeTrace!=null&&activeTrace.recordingId.equals(id)?0:clock();
            radio.send(new byte[] {7});
            report("Pedido entregue à ponte. Aguardando o Dot.");
            scheduleSoon();
          }
        });
      } catch (Exception e) {
        h.post(() -> {
          if (!closed && version == generation)
            fail("Entrega não confirmada. Confira o app.");
        });
      } finally {
        Arrays.fill(audio, (byte)0);
      }
    });
  }
  void choice(int action, int tag) {
    if (action == 7) {
      poll();
      return;
    }
    if (tag != activeTag || activeId.isEmpty())
      return;
    String option = action == 4   ? "dismiss"
                    : action == 5 ? "yes"
                    : action == 6 ? "no"
                                  : "";
    if (option.isEmpty())
      return;
    if (notice != null && (notice.decision ? action != 5 && action != 6 : action != 4))
      return;
    // A late tap on an already delivered notice cannot answer the next card.
    if (activeId.equals(receiptedId)) {
      poll();
      return;
    }
    if (!activeId.equals("@teste")) {
      boolean saved = pref()
          .edit()
          .putString("ack_id", activeId)
          .putString("ack_option", option)
          .commit();
      if (!saved) {
        activeId = "";
        notice = null;
        activeTrace = null;
        report("Não consegui guardar sua escolha. Confirme novamente na placa.");
        scheduleSoon();
        return;
      }
    }
    activeId = "";
    notice = null;
    activeTrace = null;
    if (output != null)
      output.cancel();
    releaseWake();
    scheduleSoon();
  }
  void poll() {
    if (closed || !radio.active || polling)
      return;
    final String base = pref().getString("url", "");
    if (base.isEmpty()) {
      setOnline(false);
      if (!activeId.equals("@teste"))
        report("Bluetooth pareado. Configure a ponte para acessar seu Dot.");
      return;
    }
    polling = true;
    long version = generation;
    net.execute(() -> {
      try {
        String receiptId = pref().getString("receipt_id", "");
        if (!receiptId.isEmpty()) {
          JSONObject receipt = new JSONObject().put("id", receiptId)
              .put("stage", pref().getString("receipt_stage", "displayed"));
          api(base, "POST", "/receipt", receipt.toString().getBytes(StandardCharsets.UTF_8), "application/json");
          if (pref().getString("receipt_id", "").equals(receiptId))
            pref().edit().remove("receipt_id").remove("receipt_stage").apply();
        }
        String ackId = pref().getString("ack_id", "");
        if (!ackId.isEmpty()) {
          JSONObject ack =
              new JSONObject()
                  .put("id", ackId)
                  .put("option", pref().getString("ack_option", ""));
          api(base, "POST", "/choice",
              ack.toString().getBytes(StandardCharsets.UTF_8),
              "application/json");
          if (pref().getString("ack_id", "").equals(ackId))
            pref().edit().remove("ack_id").remove("ack_option").apply();
        }
        long pollStart = clock();
        JSONObject r = api(base, "GET", "/state", null, null);
        lastPollMs = clock() - pollStart;
        h.post(() -> {
          if (closed || version != generation)
            return;
          activeBase = base;
          receipts = r.optBoolean("receipts", false);
          telemetrySupported = r.optBoolean("telemetry", false);
          localInput = "text-v1".equals(r.optString("input"));
          boolean correctMode = localInput || !OfflineVoice.siteAddress(base);
          boolean google = cloudInput();
          boolean modelReady =
              !localInput || google || OfflineVoice.ready(this);
          boolean inputValid =
              !google ||
              (localInput && SecureConfig.hasProvider(this, "google"));
          if (localInput && modelReady && !google)
            prewarm();
          boolean ok =
              r.optBoolean("ready") && correctMode && modelReady && inputValid;
          setOnline(ok);
          JSONObject card = r.optJSONObject("card");
          if (card != null) {
            String id = card.optString("id");
            if (!id.equals(activeId)) {
              activeId = id;
              activeTag = new Random().nextInt();
              String text = card.optString("text");
              notice = new NoticeReceipt(id, activeTag, card.optBoolean("decision"));
              remember(id, text, notice.decision);
              activeTrace = TraceCorrelation.reply(requests,card.isNull("recording_id")?"":card.optString("recording_id"),id);
              if (activeTrace != null) {
                activeTrace.bridgeAge(card.optLong("created_at"),card.optLong("bridge_observed_at"));
                timing = activeTrace;
                trace(activeTrace, Timings.REPLY);
                if (latestInput!=null&&activeTrace.recordingId.equals(latestInput.recordingId)) awaiting = 0;
              }else{
                android.util.Log.w("MeuDotTiming","No recording correlation for request="+id);
              }
              radio.card(notice.decision ? 1 : 0, activeTag, text);
              if (!pref().getBoolean("sound", true))
                releaseWake();
              output.speak(text, false);
              report("Mensagem do Dot recebida.");
            }
          } else if (!ok) {
            String reason = r.optString("reason");
            if (!correctMode || "client_update".equals(reason))
              report("Atualize o cliente Android pelo painel da ponte.");
            else if (!inputValid)
              report("Configure a chave do Gemini e use uma ponte compatível "
                     + "com texto.");
            else if (!modelReady)
              report("Baixe o modelo de voz em português no app Android.");
            else if ("dot_subscription".equals(reason))
              report("Celular conectado. Abra o painel e habilite os eventos "
                     + "na conversa do seu Dot.");
            else
              report("Confira a transcrição e a assinatura de voz na ponte.");
          } else if (!Boolean.TRUE.equals(r.optBoolean("has_recent_voice")) &&
                     activeId.isEmpty())
            report("Pronto. Segure o personagem na Waveshare.");
        });
      } catch (Exception e) {
        h.post(() -> {
          if (!closed && version == generation) {
            setOnline(false);
            if (e instanceof HttpError && (((HttpError)e).status == 401 ||
                                           ((HttpError)e).status == 403)) {
              report("Token recusado. Abra o painel e salve o token do "
                     + "celular novamente.");
            } else {
              report("A ponte está indisponível. Confira o endereço e a "
                     + "internet do celular.");
            }
          }
        });
      } finally {
        h.post(() -> {
          polling = false;
          if (pollAgain) { pollAgain = false; scheduleSoon(); }
        });
      }
    });
  }
}
