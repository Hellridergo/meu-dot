package br.bolso.dot;
import java.io.IOException;
import java.nio.*;
import java.nio.charset.StandardCharsets;

/**
 * Read the PCM produced by Android TTS and send bounded mono 16 kHz G.711
 * µ-law.
 */
final class SpeechAudio {
  static int encode(int v) {
    int mask = v < 0 ? 0x7f : 0xff;
    v = Math.min(Math.abs(v), 32635) + 132;
    int e = 7;
    for (int b = 16384; e > 0 && (v & b) == 0; b >>= 1)
      e--;
    return ((e << 4) | ((v >> (e + 3)) & 15)) ^ mask;
  }
  static byte[] fromWav(byte[] raw) throws IOException {
    ByteBuffer b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
    int rate = 0, channels = 0, bits = 0, format = 0, pos = -1, size = 0;
    if (raw.length < 44 || b.getInt(0) != 0x46464952 ||
        b.getInt(8) != 0x45564157)
      throw new IOException();
    for (int i = 12; i + 8 <= raw.length;) {
      int n = b.getInt(i + 4);
      if (n < 0 || n > raw.length - i - 8)
        throw new IOException();
      String tag = new String(raw, i, 4, StandardCharsets.US_ASCII);
      if (tag.equals("fmt ") && n >= 16) {
        format = b.getShort(i + 8);
        channels = b.getShort(i + 10);
        rate = b.getInt(i + 12);
        bits = b.getShort(i + 22);
      }
      if (tag.equals("data")) {
        pos = i + 8;
        size = n;
        break;
      }
      i += 8 + n + (n & 1);
    }
    if (pos < 0 || channels < 1 || channels > 2 || rate < 8000 ||
        rate > 96000 || bits != 16 || format != 1)
      throw new IOException();
    int frames = size / (2 * channels);
    int count = (int)Math.min(480000, (long)frames * 16000 / rate);
    byte[] encoded = new byte[count];
    for (int i = 0; i < count; i++) {
      int src = (int)((long)i * rate / 16000);
      int v = b.getShort(pos + src * 2 * channels);
      if (channels == 2)
        v = (v + b.getShort(pos + src * 4 + 2)) / 2;
      encoded[i] = (byte)encode(v);
    }
    return encoded;
  }
}
