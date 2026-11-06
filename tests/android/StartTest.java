package br.bolso.dot;
public class StartTest {
  static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); System.out.println("PASS " + what); }
  public static void main(String[] a) {
    SpeechTransfer t = new SpeechTransfer();
    VoiceBuffer b = new VoiceBuffer("x", 0);
    t.begin(b);
    b.append(new byte[4800]);                       // 0.3 s: first words only
    check(t.next(true, 247) == null, "streaming waits with 0.3 s of speech");
    b.append(new byte[11200]);                      // now exactly 1 s
    byte[] open = t.next(true, 247);
    check(open != null && open[0] == 0x43, "streaming opens at 1 s of speech");
    byte[] data = t.next(true, 247);
    check(data != null && data[0] == 0x44 && data.length == 241, "first packet carries 240 bytes");
    SpeechTransfer s = new SpeechTransfer();
    VoiceBuffer shortReply = new VoiceBuffer("y", 0);
    s.begin(shortReply);
    shortReply.append(new byte[6000]);
    check(s.next(true, 247) == null, "short reply waits while still synthesizing");
    shortReply.finish();
    byte[] o = s.next(true, 247);
    check(o != null && o[0] == 0x43, "short reply opens once complete, below 1 s");
    SpeechTransfer l = new SpeechTransfer();
    VoiceBuffer legacy = new VoiceBuffer("z", 0);
    l.begin(legacy);
    legacy.append(new byte[20000]);
    check(l.next(false, 247) == null, "legacy framing still waits for the end");
    legacy.finish();
    byte[] h = l.next(false, 247);
    check(h != null && h[0] == 0x40 && h.length == 5, "legacy header carries the size");
  }
}
