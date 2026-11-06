package br.bolso.dot;

import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import javax.net.ssl.*;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.drafts.Draft_6455;
import org.java_websocket.handshake.ServerHandshake;
import org.java_websocket.protocols.Protocol;
import org.json.*;
import org.msgpack.core.*;

/** Only fixed, provider-owned TLS origins receive provider keys. */
final class CloudVoice {
  static final String GEMINI =
      "https://generativelanguage.googleapis.com/v1beta/interactions";
  static final String FISH = "wss://api.fish.audio/v1/tts/live";
  static final String STT = "gemini-3.5-transcribe",
                      TTS = "gemini-3.8-flash-lite-tts",
                      FISH_MODEL = "s2.1-pro";
  interface Sink {
    void format(int rate, int channels, int encoding) throws IOException;
    void pcm(byte[] bytes) throws IOException;
    boolean stop();
    void interruptWith(Runnable action);
  }

  static JSONObject transcriptionRequest(byte[] ulaw) throws Exception {
    byte[] wav = PcmStream.wav(ulaw);
    JSONObject block =
        new JSONObject()
            .put("type", "audio")
            .put("mime_type", "audio/wav")
            .put("data", Base64.getEncoder().encodeToString(wav));
    Arrays.fill(wav, (byte)0);
    return new JSONObject()
        .put("model", STT)
        .put("store", false)
        .put("input", new JSONArray().put(block));
  }
  static JSONObject speechRequest(String text, String voice)
      throws JSONException {
    JSONObject block =
        new JSONObject()
            .put("type", "text")
            .put("text", text)
            .put("annotations",
                 new JSONArray().put(
                     new JSONObject()
                         .put("type", "speech_metadata")
                         .put("style",
                              "Português brasileiro, voz natural e direta.")));
    return new JSONObject()
        .put("model", TTS)
        .put("stream", true)
        .put("store", false)
        .put("input", new JSONArray().put(
                          new JSONObject()
                              .put("type", "user_input")
                              .put("content", new JSONArray().put(block))))
        .put("response_format", new JSONObject()
                                    .put("type", "audio")
                                    .put("mime_type", "audio/l16")
                                    .put("sample_rate", 16000))
        .put("generation_config",
             new JSONObject().put(
                 "speech_config",
                 new JSONArray().put(new JSONObject().put("voice", voice))));
  }
  static String transcript(JSONObject result) throws IOException {
    if (result.has("error") ||
        !"completed".equals(result.optString("status", "completed")))
      throw new IOException("O Google não concluiu a transcrição.");
    StringBuilder text = new StringBuilder();
    JSONArray steps = result.optJSONArray("steps");
    for (int i = 0; steps != null && i < steps.length(); i++) {
      JSONObject step = steps.optJSONObject(i);
      if (step == null || !"model_output".equals(step.optString("type")))
        continue;
      JSONArray content = step.optJSONArray("content");
      for (int j = 0; content != null && j < content.length(); j++) {
        JSONObject c = content.optJSONObject(j);
        if (c != null && "text".equals(c.optString("type"))) {
          if (text.length() > 0)
            text.append(' ');
          text.append(c.optString("text", ""));
        }
      }
    }
    if (text.length() == 0 && result.has("output_text"))
      text.append(result.optString("output_text", ""));
    return text.toString().trim();
  }
  private static IOException providerStatus(String p, int status) {
    String message = (status == 401 || status == 403)
                         ? "Confira a chave e as permissões da conta."
                     : status == 402 ? "Confira os créditos da sua conta."
                     : status == 429 ? "Limite de uso atingido. Tente depois."
                     : status == 400 || status == 404
                         ? "Confira a voz escolhida e o acesso ao modelo."
                         : "Serviço indisponível.";
    return new IOException(p + " (" + status + "). " + message);
  }
  private static HttpsURLConnection callGoogle(String key, JSONObject data,
                                               Sink sink) throws Exception {
    HttpsURLConnection c =
        (HttpsURLConnection) new URL(GEMINI).openConnection();
    c.setInstanceFollowRedirects(false);
    c.setConnectTimeout(10000);
    c.setReadTimeout(30000);
    c.setRequestMethod("POST");
    c.setDoOutput(true);
    c.setRequestProperty("Content-Type", "application/json");
    c.setRequestProperty("x-goog-api-key", key);
    if (sink != null)
      c.setRequestProperty("Accept", "text/event-stream");
    if (sink != null)
      sink.interruptWith(c::disconnect);
    boolean ok = false;
    try {
      if (sink != null && sink.stop())
        throw new IOException("Leitura cancelada.");
      byte[] bytes = data.toString().getBytes(StandardCharsets.UTF_8);
      c.setFixedLengthStreamingMode(bytes.length);
      try (OutputStream os = c.getOutputStream()) {
        os.write(bytes);
      } finally {
        Arrays.fill(bytes, (byte)0);
      }
      int status = c.getResponseCode();
      if (status < 200 || status >= 300)
        throw providerStatus("Google", status);
      ok = true;
      return c;
    } finally {
      if (!ok)
        c.disconnect();
    }
  }
  static String transcribe(String key, byte[] ulaw) throws Exception {
    HttpsURLConnection c = callGoogle(key, transcriptionRequest(ulaw), null);
    try (InputStream in = c.getInputStream();
         ByteArrayOutputStream out = new ByteArrayOutputStream()) {
      byte[] b = new byte[4096];
      for (int n; (n = in.read(b)) != -1;) {
        if (out.size() + n > 524288)
          throw new IOException("Resposta de transcrição muito longa.");
        out.write(b, 0, n);
      }
      return transcript(new JSONObject(out.toString("UTF-8")));
    } finally {
      c.disconnect();
    }
  }
  static void googleSpeech(String key, String text, String voice, Sink sink)
      throws Exception {
    HttpsURLConnection c = callGoogle(key, speechRequest(text, voice), sink);
    try (InputStream in = c.getInputStream();
         Reader r = new InputStreamReader(in, StandardCharsets.UTF_8)) {
      readEvents(r, sink);
    } finally {
      c.disconnect();
    }
  }
  static void readEvents(Reader r, Sink sink) throws Exception {
    // Each SSE event is dispatched only at its blank line. Ignore snapshots on
    // completion.
    BufferedReader lines = new BufferedReader(r);
    StringBuilder data = new StringBuilder();
    boolean completed = false;
    int read = 0;
    for (String line; !sink.stop() && (line = lines.readLine()) != null;) {
      read += line.length();
      if (read > 16000000 || line.length() > 4000000)
        throw new IOException("Resposta do Google muito longa.");
      if (line.startsWith("data:")) {
        if (data.length() > 0)
          data.append('\n');
        data.append(line.substring(5).trim());
        continue;
      }
      if (!line.isEmpty() || data.length() == 0)
        continue;
      JSONObject e = new JSONObject(data.toString());
      data.setLength(0);
      String type = e.optString("event_type");
      if (type.equals("error") || type.equals("interaction.failed"))
        throw new IOException("Falha durante a leitura do Google.");
      if (type.equals("interaction.completed")) {
        completed = e.optJSONObject("interaction") == null ||
                    "completed".equals(e.getJSONObject("interaction")
                                             .optString("status", "completed"));
        break;
      }
      if (!type.equals("step.delta"))
        continue;
      JSONObject d = e.optJSONObject("delta");
      if (d == null || !"audio".equals(d.optString("type")))
        continue;
      String mime =
          d.optString("mime_type", "audio/l16").toLowerCase(Locale.ROOT);
      if (!mime.startsWith("audio/l16") && !mime.startsWith("audio/pcm"))
        throw new IOException("O Google retornou um formato inesperado.");
      int rate = d.optInt("sample_rate", 16000);
      for (String param : mime.split(";"))
        if (param.trim().startsWith("rate="))
          rate = Integer.parseInt(param.trim().substring(5));
      sink.format(rate, 1, PcmStream.PCM_16);
      sink.pcm(Base64.getDecoder().decode(d.getString("data")));
    }
    if (!completed && !sink.stop())
      throw new IOException("A transmissão do Google foi interrompida.");
  }

  static byte[] fishEvent(String event, String value) throws IOException {
    try (MessageBufferPacker p = MessagePack.newDefaultBufferPacker()) {
      if (event.equals("start")) {
        p.packMapHeader(2)
            .packString("event")
            .packString(event)
            .packString("request")
            .packMapHeader(6)
            .packString("text")
            .packString("")
            .packString("format")
            .packString("pcm")
            .packString("sample_rate")
            .packInt(16000)
            .packString("latency")
            .packString("low")
            .packString("chunk_length")
            .packInt(100)
            .packString("reference_id")
            .packString(value);
      } else if (event.equals("text")) {
        p.packMapHeader(2)
            .packString("event")
            .packString(event)
            .packString("text")
            .packString(value);
      } else {
        p.packMapHeader(1).packString("event").packString(event);
      }
      return p.toByteArray();
    }
  }
  static boolean readFish(ByteBuffer packet, Sink sink) throws IOException {
    if (packet.remaining() > 2000000)
      throw new IOException("Bloco Fish muito longo.");
    try (MessageUnpacker u = MessagePack.newDefaultUnpacker(packet)) {
      int fields = u.unpackMapHeader();
      if (fields > 16)
        throw new IOException("Pacote Fish inválido.");
      String type = "", reason = "";
      byte[] audio = null;
      for (int i = 0; i < fields; i++) {
        String k = u.unpackString();
        switch (k) {
        case "event":
          type = u.unpackString();
          break;
        case "audio":
          int len = u.unpackBinaryHeader();
          if (len < 0 || len > 2000000)
            throw new IOException("Bloco Fish inválido.");
          audio = u.readPayload(len);
          break;
        case "reason":
          reason = u.unpackString();
          break;
        default:
          u.skipValue();
        }
      }
      if (type.equals("audio")) {
        if (audio == null)
          throw new IOException("Áudio Fish incompleto.");
        sink.format(16000, 1, PcmStream.PCM_16);
        sink.pcm(audio);
      } else if (type.equals("error") ||
                 (type.equals("finish") && !reason.equals("stop")))
        throw new IOException(
            "Fish Audio recusou a síntese. Confira o ID da voz e os créditos.");
      return type.equals("finish") || sink.stop();
    } catch (RuntimeException bad) {
      throw new IOException("Resposta MessagePack inválida.");
    }
  }
  static void fishSpeech(String key, String text, String voice, Sink sink)
      throws Exception {
    if (!voice.matches("[a-fA-F0-9]{32}"))
      throw new IOException("Informe o ID da voz Fish.");
    CountDownLatch ended = new CountDownLatch(1);
    AtomicReference<IOException> error = new AtomicReference<>();
    Map<String, String> headers = new HashMap<>();
    headers.put("Authorization", "Bearer " + key);
    headers.put("model", FISH_MODEL);
    Draft_6455 draft =
        new Draft_6455(Collections.emptyList(),
                       Collections.singletonList(new Protocol("")), 2000000);
    WebSocketClient ws = new WebSocketClient(URI.create(FISH), draft, headers,
                                             10000) {
      private volatile boolean finished;
      private void abort(IOException e) {
        error.compareAndSet(null, e);
        ended.countDown();
        close();
      }
      public void onOpen(ServerHandshake response) {
        try {
          if (sink.stop()) {
            ended.countDown();
            close();
            return;
          }
          send(fishEvent("start", voice));
          send(fishEvent("text", text));
          send(fishEvent("flush", ""));
          send(fishEvent("stop", ""));
        } catch (Exception e) {
          abort(new IOException("Não consegui iniciar a voz Fish."));
        }
      }
      public void onMessage(String s) {
        abort(new IOException("Formato Fish inesperado."));
      }
      public void onMessage(ByteBuffer b) {
        try {
          if (readFish(b, sink)) {
            finished = true;
            ended.countDown();
            close();
          }
        } catch (IOException e) {
          abort(e);
        }
      }
      public void onClose(int code, String reason, boolean remote) {
        if (!finished && !sink.stop())
          error.compareAndSet(
              null, new IOException("Fish Audio interrompeu a conexão. "
                                    + "Confira chave, créditos e voz."));
        ended.countDown();
      }
      public void onError(Exception e) {
        abort(new IOException(
            "Conexão Fish Audio indisponível. Confira a chave e a internet."));
      }
    };
    ws.setSocketFactory(SSLSocketFactory.getDefault());
    ws.setConnectionLostTimeout(18);
    sink.interruptWith(() -> {
      ws.close();
      ended.countDown();
    });
    try {
      if (sink.stop())
        return;
      ws.connect();
      if (!ended.await(60, TimeUnit.SECONDS))
        throw new IOException("A síntese Fish excedeu o tempo limite.");
      if (error.get() != null && !sink.stop())
        throw error.get();
    } finally {
      ws.close();
    }
  }
}
