package br.bolso.dot;
import android.content.*;
import android.media.AudioFormat;
import android.os.*;
import android.speech.tts.*;
import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A single utterance at a time. All control messages and GATT writes use the
 * main looper.
 */
final class VoiceOutput {
  final Companion owner;
  private TextToSpeech engine;
  private boolean offlinePt;
  private volatile Job current;
  private final ExecutorService workers = Executors.newCachedThreadPool();
  private final AtomicBoolean posted = new AtomicBoolean();

  VoiceOutput(Companion c) {
    owner = c;
    engine = new TextToSpeech(c, status -> owner.h.post(() -> {
      if (status != TextToSpeech.SUCCESS || engine == null || owner.closed)
        return;
      Set<Voice> list = engine.getVoices();
      if (list == null)
        return;
      Voice best = null;
      for (Voice v : list) {
        Locale l = v.getLocale();
        if (l == null || !"pt".equals(l.getLanguage()) ||
            v.isNetworkConnectionRequired() ||
            v.getFeatures() != null &&
                v.getFeatures().contains(
                    TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED))
          continue;
        if (best == null ||
            ("BR".equals(l.getCountry()) &&
             !"BR".equals(best.getLocale().getCountry())) ||
            (l.getCountry().equals(best.getLocale().getCountry()) &&
             v.getQuality() > best.getQuality()))
          best = v;
      }
      offlinePt = best != null && engine.setVoice(best) == TextToSpeech.SUCCESS;
    }));
    engine.setOnUtteranceProgressListener(new UtteranceProgressListener() {
      public void onStart(String id) {}
      public void onBeginSynthesis(String id, int rate, int encoding,
                                   int channels) {
        Job j = job(id);
        if (j == null)
          return;
        try {
          j.format(rate, channels, encoding);
        } catch (IOException ex) {
          j.platformFormatUnsupported = true;
        }
      }
      public void onAudioAvailable(String id, byte[] audio) {
        Job j = job(id);
        if (j == null || j.platformFormatUnsupported || j.converter == null)
          return;
        try {
          j.pcm(audio);
        } catch (IOException ex) {
          problem(j, ex.getMessage());
        }
      }
      public void onDone(String id) {
        Job j = job(id);
        if (j == null)
          return;
        workers.execute(() -> {
          try {
            if (j.buffer.size() == 0 && !j.stop()) {
              File file = j.file;
              if (file == null || file.length() > 12000000)
                throw new IOException("Áudio Android inválido.");
              byte[] bytes =
                  SpeechAudio.fromWav(Files.readAllBytes(file.toPath()));
              j.append(bytes);
            }
            done(j);
          } catch (Exception ex) {
            problem(j, "A síntese Android não produziu áudio compatível.");
          } finally {
            j.removeFile();
          }
        });
      }
      public void onError(String id) {
        Job j = job(id);
        if (j != null)
          problem(j, "O Android não conseguiu gerar a fala. Confira a voz "
                         + "pt-BR instalada.");
      }
    });
  }
  private Job job(String id) {
    Job j = current;
    return j != null && j.buffer.id.equals(id) && !j.buffer.cancelled ? j
                                                                      : null;
  }
  private void wake() {
    if (posted.compareAndSet(false, true))
      owner.h.post(() -> {
        posted.set(false);
        if (!owner.closed && owner.radio != null)
          owner.radio.pump();
      });
  }
  final class Job implements CloudVoice.Sink {
    final VoiceBuffer buffer;
    volatile Runnable interrupt;
    volatile File file;
    volatile PcmStream converter;
    volatile boolean platformFormatUnsupported;
    // Cloud jobs only: what to read again with the Android voice on failure.
    volatile String text, provider;
    int rate, ch, enc;
    final Timings trace;
    Job(String id) {
      buffer = new VoiceBuffer(id, SystemClock.elapsedRealtime());
      trace = owner.activeTrace;
    }
    public synchronized void format(int sampleRate, int channels, int encoding)
        throws IOException {
      if (converter == null) {
        converter = new PcmStream(sampleRate, channels, encoding);
        rate = sampleRate;
        ch = channels;
        enc = encoding;
      } else if (sampleRate != rate || ch != channels || enc != encoding)
        throw new IOException("O formato de áudio mudou durante a fala.");
    }
    public synchronized void pcm(byte[] b) throws IOException {
      if (stop())
        return;
      if (converter == null)
        throw new IOException("Formato de áudio ausente.");
      append(converter.convert(b));
    }
    synchronized void append(byte[] b) {
      if (!buffer.append(b))
        return;
      owner.trace(trace, Timings.SYNTH);
      wake();
    }
    public boolean stop() {
      return owner.closed || buffer.cancelled ||
          buffer.size() >= PcmStream.LIMIT || current != this;
    }
    public void interruptWith(Runnable r) {
      interrupt = r;
      if (buffer.cancelled || current != this)
        r.run();
    }
    void removeFile() {
      File f = file;
      if (f != null) {
        f.delete();
        file = null;
      }
    }
  }
  void speak(String text, boolean bench) {
    cancel();
    if (owner.closed || !owner.pref().getBoolean("sound", true) ||
        text.trim().isEmpty())
      return;
    SharedPreferences v = owner.getSharedPreferences("voice", 0);
    String provider = bench ? "android" : v.getString("tts", "android");
    Job j = new Job(UUID.randomUUID().toString());
    if (j.trace != null) j.trace.output = provider;
    current = j;
    owner.radio.startAudio(j.buffer, j.trace, owner.activeTag);
    if (provider.equals("android")) {
      if (!offlinePt || engine == null) {
        problem(j, "Instale uma voz offline em português no mecanismo de "
                       + "síntese do Android.");
        return;
      }
      try {
        j.file = File.createTempFile("fala-", ".wav", owner.getCacheDir());
        Bundle options = new Bundle();
        options.putString(TextToSpeech.Engine.KEY_FEATURE_EMBEDDED_SYNTHESIS,
                          "true");
        owner.trace(j.trace,Timings.TTS_START);
        if (engine.synthesizeToFile(text, options, j.file, j.buffer.id) !=
            TextToSpeech.SUCCESS)
          throw new IOException();
      } catch (IOException ex) {
        problem(j, "O Android não conseguiu iniciar a leitura.");
      }
      return;
    }
    if (!v.getBoolean("consent", false)) {
      problem(j, "Autorize o provedor em Configurações de voz.");
      return;
    }
    j.text = text;
    j.provider = provider;
    String voice = v.getString("gemini_voice", "Kore"),
           fish = v.getString("fish_id", "");
    // 0.5.2: a cloud voice that sends nothing within 8 s is cut off, so the
    // reply falls back to the Android voice instead of staying silent.
    owner.h.postDelayed(() -> {
      if (current != j || j.buffer.cancelled || j.buffer.size() > 0)
        return;
      android.util.Log.w("MeuDotVoz", "voz " + provider + " sem áudio em 8 s");
      Runnable stall = j.interrupt;
      if (stall != null)
        workers.execute(stall);
    }, 8000);
    workers.execute(() -> {
      try {
        if (j.stop())
          return;
        owner.trace(j.trace,Timings.TTS_START);
        if ("google".equals(provider))
          CloudVoice.googleSpeech(SecureConfig.provider(owner, "google"), text,
                                  voice, j);
        else if ("fish".equals(provider))
          CloudVoice.fishSpeech(SecureConfig.provider(owner, "fish"), text,
                                fish, j);
        else
          throw new IOException("Provedor de voz desconhecido.");
        done(j);
      } catch (Exception e) {
        String safe = "Falha na leitura em nuvem. Confira a chave, a voz, os "
                      + "créditos e a rede.";
        // Only our own constant provider errors are safe to show. Never display
        // wire payloads or URLs from exceptions.
        if (e.getClass() == IOException.class && e.getMessage() != null &&
            (e.getMessage().startsWith("Google (") ||
             e.getMessage().startsWith("Fish Audio ") ||
             e.getMessage().startsWith("A transmissão do Google")))
          safe = e.getMessage();
        if (!fallback(j, safe))
          problem(j, safe);
      }
    });
  }
  /**
   * 0.5.2: when a cloud voice fails before producing any audio, read the same
   * reply with the offline Android voice. A partial reply is not restarted.
   */
  private boolean fallback(Job j, String reason) {
    if (j.provider == null || j.text == null || !offlinePt || engine == null ||
        j.buffer.size() > 0 || j.buffer.cancelled || current != j || owner.closed)
      return false;
    android.util.Log.w("MeuDotVoz", "voz " + j.provider + " falhou (" + reason
                                        + "); lendo com a voz do Android");
    String text = j.text;
    owner.h.post(() -> {
      if (current != j || j.buffer.cancelled || owner.closed)
        return;
      owner.telemetry(j.trace, "tts_fallback");
      speak(text, true);
    });
    return true;
  }
  private void done(Job j) {
    if (j.buffer.cancelled || current != j)
      return;
    if (j.buffer.size() == 0) {
      String reason = "O provedor concluiu sem áudio. Confira a voz selecionada.";
      if (!fallback(j, reason))
        problem(j, reason);
      return;
    }
    if (j.converter != null && j.converter.incomplete() &&
        !j.converter.full()) {
      problem(j, "A leitura chegou incompleta.");
      return;
    }
    j.buffer.finish();
    owner.trace(j.trace,Timings.TTS_DONE);
    wake();
    keepLast(j);
  }
  /**
   * 0.5.2 diagnostics: the exact µ-law sent to the board, as a WAV that adb can
   * pull from Android/data/br.bolso.dot/files. Only the latest reply is kept.
   */
  private void keepLast(Job j) {
    final byte[] audio = j.buffer.copy();
    final String provider = j.provider == null ? "android" : j.provider;
    workers.execute(() -> {
      File dir = owner.getExternalFilesDir(null);
      if (dir == null || audio.length < 4000)
        return;
      File out = new File(dir, "ultima-resposta.wav");
      try (FileOutputStream f = new FileOutputStream(out)) {
        f.write(PcmStream.wav(audio));
        android.util.Log.i("MeuDotVoz", "voz " + provider + ": " + audio.length
                                            + " bytes, " + audio.length / 16
                                            + " ms, salvo em " + out.getName());
      } catch (IOException ex) {
        android.util.Log.w("MeuDotVoz", "não salvei a última resposta");
      }
    });
  }
  private void problem(Job j, String text) {
    android.util.Log.w("MeuDotVoz", "falha na voz: " + text);
    owner.h.post(() -> {
      if (current != j || j.buffer.cancelled || owner.closed)
        return;
      owner.telemetry(j.trace,"tts_failed");
      cancel();
      owner.releaseWake();
      owner.report(text);
    });
  }
  void cancel() {
    Job old = current;
    current = null;
    if (old == null)
      return;
    old.buffer.cancel();
    Runnable interrupt = old.interrupt;
    if (interrupt != null)
      workers.execute(interrupt);
    if (engine != null)
      engine.stop();
    old.removeFile();
    if (owner.radio != null)
      owner.radio.cancelAudio();
  }
  void close() {
    cancel();
    if (engine != null) {
      engine.shutdown();
      engine = null;
    }
    workers.shutdown();
  }
}
