package br.bolso.dot;
import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.json.*;
import org.msgpack.core.*;
import org.msgpack.value.*;

public final class VoiceContracts {
  static void check(boolean ok, String s) {
    if (!ok)
      throw new AssertionError(s);
  }
  static final class Sink implements CloudVoice.Sink {
    PcmStream codec;
    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    int chunks;
    public void format(int r, int c, int e) throws IOException {
      if (codec == null)
        codec = new PcmStream(r, c, e);
    }
    public void pcm(byte[] b) throws IOException {
      chunks++;
      byte[] v = codec.convert(b);
      bytes.write(v, 0, v.length);
    }
    public boolean stop() { return codec != null && codec.full(); }
    public void interruptWith(Runnable r) {}
  }
  static byte[] fish(String kind, byte[] bytes, String reason)
      throws Exception {
    try (MessageBufferPacker p = MessagePack.newDefaultBufferPacker()) {
      p.packMapHeader(2).packString("event").packString(kind);
      if (bytes != null) {
        p.packString("audio")
            .packBinaryHeader(bytes.length)
            .writePayload(bytes);
      } else
        p.packString("reason").packString(reason);
      return p.toByteArray();
    }
  }
  static String delta(byte[] b) throws Exception {
    return "event: step.delta\ndata: " +
        new JSONObject()
            .put("event_type", "step.delta")
            .put("delta",
                 new JSONObject()
                     .put("type", "audio")
                     .put("mime_type", "audio/l16;rate=16000")
                     .put("data", Base64.getEncoder().encodeToString(b))) +
        "\n\n";
  }
  static byte[] decodePackets(SpeechTransfer transfer, boolean capable,
                              int expectedStart, int expectedEnd,
                              boolean alreadyStarted) throws Exception {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    boolean start = alreadyStarted, end = false;
    for (byte[] packet; (packet = transfer.next(capable, 247)) != null;) {
      int op = packet[0] & 255;
      if (!start) {
        check(op == expectedStart, "start");
        start = true;
      } else if (op == expectedEnd)
        end = true;
      else {
        check(op == (capable ? 0x44 : 0x41), "data op");
        check(packet.length <= 241, "MTU");
        out.write(packet, 1, packet.length - 1);
      }
    }
    check(start && end, "complete");
    return out.toByteArray();
  }
  public static void main(String[] args) throws Exception {
    byte[] words = new byte[480000];
    Arrays.fill(words, (byte)128);
    byte[] wav = PcmStream.wav(words);
    check(wav.length == 960044 &&
              ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN).getInt(24) ==
                  16000,
          "wav 30 seconds");
    boolean rejected = false;
    try {
      PcmStream.wav(new byte[480001]);
    } catch (IOException good) {
      rejected = true;
    }
    check(rejected, "31st second");
    JSONObject req = CloudVoice.transcriptionRequest(words);
    check(req.getString("model").equals("gemini-3.5-transcribe") &&
              !req.getBoolean("store"),
          "STT contract");
    check(Arrays.equals(
              wav, Base64.getDecoder().decode(
                       req.getJSONArray("input").getJSONObject(0).getString(
                           "data"))),
          "audio payload");
    String answer = CloudVoice.transcript(
        new JSONObject("{\"status\":\"completed\",\"steps\":[{\"type\":" +
                       "\"model_output\",\"content\":[{\"type\":\"text\"," +
                       "\"text\":\"comando em português\"}]}]}"));
    check(answer.equals("comando em português"), "parse STT");
    JSONObject tr = CloudVoice.speechRequest("Atenção.", "Kore");
    check(tr.getBoolean("stream") &&
              tr.getString("model").equals("gemini-3.8-flash-lite-tts") &&
              tr.getJSONObject("response_format").getInt("sample_rate") ==
                  16000,
          "TTS contract");
    byte[] pcm = new byte[32000];
    new Random(9).nextBytes(pcm);
    byte[] expected = new PcmStream(16000, 1, PcmStream.PCM_16).convert(pcm);
    Sink google = new Sink();
    CloudVoice.readEvents(
        new StringReader(delta(Arrays.copyOfRange(pcm, 0, 33)) +
                         delta(Arrays.copyOfRange(pcm, 33, pcm.length)) +
                         ("event: interaction.completed\ndata: " +
                          "{\"event_type\":\"interaction.completed\"," +
                          "\"interaction\":{\"status\":\"completed\"}}\n\n")),
        google);
    check(google.chunks == 2 &&
              Arrays.equals(expected, google.bytes.toByteArray()),
          "Gemini progressive");
    rejected = false;
    try {
      CloudVoice.readEvents(new StringReader(delta(pcm)), new Sink());
    } catch (IOException good) {
      rejected = true;
    }
    check(rejected, "SSE premature EOF");
    byte[] start =
        CloudVoice.fishEvent("start", "1a2b3c4d1a2b3c4d1a2b3c4d1a2b3c4d");
    try (MessageUnpacker u = MessagePack.newDefaultUnpacker(start)) {
      JSONObject f = new JSONObject(u.unpackValue().toJson());
      JSONObject config = f.getJSONObject("request");
      check(f.getString("event").equals("start") &&
                config.getString("format").equals("pcm") &&
                config.getInt("sample_rate") == 16000 &&
                config.getString("reference_id").length() == 32,
            "Fish start");
    }
    for (String k : new String[] {"text", "flush", "stop"}) {
      try (MessageUnpacker u = MessagePack.newDefaultUnpacker(
               CloudVoice.fishEvent(k, "Olá."))) {
        check(new JSONObject(u.unpackValue().toJson())
                  .getString("event")
                  .equals(k),
              "Fish " + k);
      }
    }
    Sink f = new Sink();
    check(!CloudVoice.readFish(
              ByteBuffer.wrap(
                  fish("audio", Arrays.copyOfRange(pcm, 0, 15), null)),
              f),
          "Fish first chunk");
    check(f.bytes.size() > 0, "decode before finish");
    CloudVoice.readFish(
        ByteBuffer.wrap(
            fish("audio", Arrays.copyOfRange(pcm, 15, pcm.length), null)),
        f);
    check(
        CloudVoice.readFish(ByteBuffer.wrap(fish("finish", null, "stop")), f) &&
            Arrays.equals(expected, f.bytes.toByteArray()),
        "Fish PCM");
    rejected = false;
    try {
      CloudVoice.readFish(ByteBuffer.wrap(fish("finish", null, "error")),
                          new Sink());
    } catch (IOException e) {
      rejected = true;
    }
    check(rejected, "Fish error");
    byte[] stereo = new byte[48000 * 4];
    new Random(11).nextBytes(stereo);
    byte[] all = new PcmStream(48000, 2, PcmStream.PCM_16).convert(stereo);
    for (int chunk : new int[] {1, 7, 39, 608, 1231}) {
      PcmStream partial = new PcmStream(48000, 2, PcmStream.PCM_16);
      ByteArrayOutputStream result = new ByteArrayOutputStream();
      for (int i = 0; i < stereo.length; i += chunk) {
        byte[] v = partial.convert(
            Arrays.copyOfRange(stereo, i, Math.min(stereo.length, i + chunk)));
        result.write(v);
      }
      check(Arrays.equals(all, result.toByteArray()) && all.length == 16000,
            "fragmented stereo resampling");
    }
    VoiceBuffer b = new VoiceBuffer("t", 1);
    SpeechTransfer x = new SpeechTransfer();
    x.begin(b);
    b.append(Arrays.copyOfRange(expected, 0, 200));
    check(x.next(false, 247) == null, "legacy waits");
    // Since 0.5.3 streaming opens with 1 s of speech or the whole reply
    // (tests/android/StartTest.java covers the threshold itself).
    check(x.next(true, 247) == null, "stream waits for 1 s of speech");
    b.append(Arrays.copyOfRange(expected, 200, expected.length));
    b.finish();
    check(Arrays.equals(expected, decodePackets(x, true, 0x43, 0x45, false)),
          "stream carries the whole reply");
    check(x.cancel(), "stop queued playback");
    check(!x.cancel(), "cancel idempotent");
    b = new VoiceBuffer("old", 2);
    x.begin(b);
    b.append(expected);
    b.finish();
    byte[] header = x.next(false, 247);
    check(header[0] == 0x40 && ByteBuffer.wrap(header, 1, 4)
                                       .order(ByteOrder.LITTLE_ENDIAN)
                                       .getInt() == expected.length,
          "legacy exact size");
    check(Arrays.equals(expected, decodePackets(x, false, 0x40, 0x42, true)),
          "legacy audio");
    b = new VoiceBuffer("bounded", 3);
    b.append(new byte[500000]);
    check(b.size() == 480000 && b.done(), "bounded producer");
    Timings m = new Timings("t", 1, 100);
    m.mark(Timings.AUDIO, 620);
    m.mark(Timings.STT_START, 620);
    m.mark(Timings.TEXT, 1200);
    check(m.summary().contains("520 ms") && m.summary().contains("580 ms"),
          "latency");
    System.out.println(
        "OK · Gemini STT/TTS contracts and SSE, Fish MessagePack, early PCM, " +
        "30 s, resampling across boundaries, BLE v2/v3 framing, " +
        "cancellation, latency. No credentials or external inference.");
  }
}
