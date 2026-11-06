package br.bolso.dot;

import java.nio.*;

/**
 * One next() per available GATT write slot. Legacy framing requires the final
 * size.
 */
final class SpeechTransfer {
  static final int START_BYTES = 16000; // 1 s of 16 kHz µ-law
  private VoiceBuffer buffer;
  private int offset;
  private boolean opened, streaming, mayBePlaying;
  long cycle;
  boolean firstPayload;

  boolean cancel() {
    if (buffer != null)
      buffer.cancel();
    buffer = null;
    boolean needsStop = mayBePlaying;
    mayBePlaying = false;
    opened = false;
    offset = 0;
    cycle++;
    return needsStop;
  }
  void begin(VoiceBuffer next) {
    buffer = next;
    offset = 0;
    opened = false;
  }
  byte[] next(boolean capable, int mtu) {
    firstPayload = false;
    if (buffer == null)
      return null;
    synchronized (buffer) {
      if (buffer.cancelled)
        return null;
      final int size = buffer.size();
      if (!opened) {
        if (size == 0 || (!capable && !buffer.done()))
          return null;
        // 0.5.3: a cloud voice sends its first words before the rest. Starting
        // at once let the board run dry after the first syllable and pause
        // for 0.7 s. Wait for 1 s of speech, or the whole reply if shorter.
        if (capable && !buffer.done() && size < START_BYTES)
          return null;
        opened = true;
        streaming = capable;
        mayBePlaying = streaming;
        if (streaming)
          return new byte[] {0x43};
        return ByteBuffer.allocate(5)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put((byte)0x40)
            .putInt(size)
            .array();
      }
      if (offset < size) {
        byte[] b = buffer.packet(offset, Math.max(16, Math.min(240, mtu - 4)),
                                 streaming ? 0x44 : 0x41);
        firstPayload = offset == 0;
        offset += b.length - 1;
        return b;
      }
      if (buffer.done()) {
        opened = false;
        buffer = null;
        return new byte[] {(byte)(streaming ? 0x45 : 0x42)};
      }
      return null;
    }
  }
}
