package br.bolso.dot;
import java.io.*;
import java.util.Arrays;

/** One bounded response. BLE pulls one frame per GATT acknowledgement. */
final class VoiceBuffer {
  private final byte[] audio = new byte[PcmStream.LIMIT];
  private int size;
  private boolean finished;
  volatile boolean cancelled;
  final String id;
  final long started;
  VoiceBuffer(String id, long started) {
    this.id = id;
    this.started = started;
  }
  synchronized int size() { return size; }
  synchronized boolean done() { return finished; }
  synchronized boolean append(byte[] b) {
    if (cancelled || finished || b.length == 0)
      return false;
    int n = Math.min(b.length, audio.length - size);
    System.arraycopy(b, 0, audio, size, n);
    size += n;
    if (size == audio.length)
      finished = true;
    return n > 0;
  }
  synchronized byte[] copy() { return Arrays.copyOf(audio, size); }
  synchronized byte[] packet(int offset, int max, int op) {
    int n = Math.min(max, size - offset);
    byte[] b = new byte[1 + n];
    b[0] = (byte)op;
    System.arraycopy(audio, offset, b, 1, n);
    return b;
  }
  synchronized void finish() { finished = true; }
  synchronized void cancel() {
    cancelled = true;
    finished = true;
  }
}
