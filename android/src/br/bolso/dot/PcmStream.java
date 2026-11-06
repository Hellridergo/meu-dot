package br.bolso.dot;

import java.io.*;
import java.util.Arrays;

/** Carries partial frames and the resampling phase across provider chunks. */
final class PcmStream {
  static final int PCM_16 = 2, PCM_8 = 3, PCM_FLOAT = 4;
  static final int RATE = 16000, LIMIT = 30 * RATE;
  private final int rate, channels, encoding, bytesPerSample;
  private final byte[] frame;
  private int partial, phase, samples;

  PcmStream(int rate, int channels, int encoding) throws IOException {
    if (rate < 8000 || rate > 96000 || channels < 1 || channels > 2 ||
        (encoding != PCM_8 && encoding != PCM_16 && encoding != PCM_FLOAT))
      throw new IOException("Formato de áudio incompatível.");
    this.rate = rate;
    this.channels = channels;
    this.encoding = encoding;
    bytesPerSample = encoding == PCM_16 ? 2 : encoding == PCM_FLOAT ? 4 : 1;
    frame = new byte[channels * bytesPerSample];
  }

  private int sample(int at) {
    if (encoding == PCM_8)
      return ((frame[at] & 255) - 128) << 8;
    int low = (frame[at] & 255) | ((frame[at + 1] & 255) << 8);
    if (encoding == PCM_16)
      return (short)low;
    int bits = low | ((frame[at + 2] & 255) << 16) | (frame[at + 3] << 24);
    float value = Float.intBitsToFloat(bits);
    return Math.round(Math.max(-1f, Math.min(1f, value)) * 32767f);
  }
  byte[] convert(byte[] pcm) throws IOException {
    if (pcm.length > 4000000)
      throw new IOException("Bloco de áudio muito longo.");
    int capacity = (int)Math.min(
        (long)LIMIT - samples,
        (long)(pcm.length + frame.length) * RATE / rate / frame.length + 3);
    byte[] result = new byte[capacity];
    int count = 0;
    for (byte value : pcm) {
      if (samples == LIMIT)
        break;
      frame[partial++] = value;
      if (partial != frame.length)
        continue;
      partial = 0;
      int mono = sample(0);
      if (channels == 2)
        mono = (mono + sample(bytesPerSample)) / 2;
      phase += RATE;
      while (phase >= rate && samples < LIMIT) {
        result[count++] = (byte)SpeechAudio.encode(mono);
        ++samples;
        phase -= rate;
      }
    }
    return Arrays.copyOf(result, count);
  }
  boolean full() { return samples == LIMIT; }
  boolean incomplete() { return partial != 0; }

  static byte[] wav(byte[] audio) throws IOException {
    if (audio.length < 4000 || audio.length > LIMIT)
      throw new IOException("Duração de voz inválida.");
    java.nio.ByteBuffer b = java.nio.ByteBuffer.allocate(44 + audio.length * 2)
                                .order(java.nio.ByteOrder.LITTLE_ENDIAN);
    b.putInt(0x46464952)
        .putInt(36 + 2 * audio.length)
        .putInt(0x45564157)
        .putInt(0x20746d66)
        .putInt(16)
        .putShort((short)1)
        .putShort((short)1)
        .putInt(RATE)
        .putInt(2 * RATE)
        .putShort((short)2)
        .putShort((short)16)
        .putInt(0x61746164)
        .putInt(2 * audio.length);
    for (byte v : audio) {
      int c = (~v) & 255, m = (((c & 15) * 8 + 132) << ((c >> 4) & 7)) - 132;
      b.putShort((short)((c & 128) != 0 ? -m : m));
    }
    return b.array();
  }
}
