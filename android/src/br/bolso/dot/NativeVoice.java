package br.bolso.dot;

import java.io.IOException;

final class NativeVoice {
  static { System.loadLibrary("dot_voice"); }
  private NativeVoice() {}
  static native void warm(String file) throws IOException;
  static native void release();
  static native byte[] transcribe(String model, byte[] audio)
      throws IOException;
}
