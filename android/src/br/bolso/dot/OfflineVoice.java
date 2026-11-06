package br.bolso.dot;

import android.content.Context;
import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.security.*;
import java.util.Locale;
import javax.net.ssl.HttpsURLConnection;

final class OfflineVoice {
  static final long SIZE = 59707625L;
  static final String HASH =
      "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898";
  static final String SOURCE =
      "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
      + "5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base-q5_1.bin";
  private static volatile int progress = -1;
  private static volatile String failure;

  private OfflineVoice() {}
  private static File model(Context c) {
    return new File(c.getNoBackupFilesDir(), "whisper-base-q5_1.bin");
  }
  static void warm(Context c) throws IOException {
    if (ready(c))
      NativeVoice.warm(model(c).getAbsolutePath());
  }
  static boolean ready(Context c) { return model(c).length() == SIZE; }
  static String state(Context c) {
    if (ready(c))
      return "Modelo de português pronto para transcrever no celular.";
    if (progress >= 0)
      return "Baixando o modelo de português… " + progress + "%";
    if (failure != null)
      return failure;
    return "Baixe o modelo uma vez para usar os comandos de voz com o Sites.";
  }
  static boolean busy() { return progress >= 0; }

  static synchronized void prepare(Context context) {
    final Context c = context.getApplicationContext();
    if (busy() || ready(c))
      return;
    failure = null;
    progress = 0;
    new Thread(() -> {
      File pending =
          new File(c.getNoBackupFilesDir(), "dot-voice-download.part");
      try {
        download(pending);
        Files.move(pending.toPath(), model(c).toPath(),
                   StandardCopyOption.ATOMIC_MOVE,
                   StandardCopyOption.REPLACE_EXISTING);
      } catch (Exception ex) {
        failure = "O download não terminou. Confira sua internet e toque "
                  + "para tentar novamente.";
      } finally {
        if (pending.exists())
          pending.delete();
        progress = -1;
      }
    }, "dot-model-download").start();
  }

  private static HttpsURLConnection openModel() throws IOException {
    URL url = new URL(SOURCE);
    for (int i = 0; i < 6; i++) {
      String host = url.getHost().toLowerCase(Locale.ROOT);
      if (!"https".equals(url.getProtocol()) || url.getUserInfo() != null ||
          (url.getPort() != -1 && url.getPort() != 443) ||
          !(host.equals("huggingface.co") || host.endsWith(".huggingface.co") ||
            host.endsWith(".hf.co"))) {
        throw new IOException("Destino do modelo inválido.");
      }
      HttpsURLConnection c = (HttpsURLConnection)url.openConnection();
      c.setInstanceFollowRedirects(false);
      c.setConnectTimeout(20000);
      c.setReadTimeout(45000);
      c.setRequestProperty("Accept-Encoding", "identity");
      try {
        int code = c.getResponseCode();
        if (code == 200)
          return c;
        String location = c.getHeaderField("Location");
        if (code < 300 || code >= 400 || location == null)
          throw new IOException("Download indisponível.");
        url = new URL(url, location);
      } catch (IOException e) {
        c.disconnect();
        throw e;
      }
      c.disconnect();
    }
    throw new IOException("Muitos redirecionamentos no download.");
  }

  private static void download(File pending) throws Exception {
    MessageDigest sum = MessageDigest.getInstance("SHA-256");
    HttpsURLConnection c = openModel();
    long total = 0;
    try (InputStream in = c.getInputStream();
         FileOutputStream out = new FileOutputStream(pending)) {
      byte[] buffer = new byte[65536];
      for (int n; (n = in.read(buffer)) != -1;) {
        total += n;
        if (total > SIZE)
          throw new IOException("Arquivo do modelo maior que o esperado.");
        sum.update(buffer, 0, n);
        out.write(buffer, 0, n);
        progress = (int)(100 * total / SIZE);
      }
      out.getFD().sync();
    } finally {
      c.disconnect();
    }
    StringBuilder hex = new StringBuilder(64);
    for (byte b : sum.digest())
      hex.append(String.format(Locale.ROOT, "%02x", b & 255));
    if (total != SIZE || !HASH.equals(hex.toString()))
      throw new IOException("Modelo incompleto.");
  }

  static boolean siteAddress(String base) {
    try {
      URI u = new URI(base);
      // Bridges hosted on ChatGPT Sites receive text (the phone transcribes).
      String host = u.getHost() == null ? "" : u.getHost().toLowerCase(Locale.ROOT);
      return "https".equalsIgnoreCase(u.getScheme()) &&
          host.endsWith(".chatgpt.site") &&
          (u.getPort() == -1 || u.getPort() == 443);
    } catch (URISyntaxException ex) {
      return false;
    }
  }

  static String transcribe(Context c, byte[] audio) throws IOException {
    if (!ready(c))
      throw new IOException("Prepare o modelo de voz no Android.");
    try {
      byte[] utf = NativeVoice.transcribe(model(c).getAbsolutePath(), audio);
      if (utf == null)
        throw new IOException("Sem transcrição.");
      return new String(utf, java.nio.charset.StandardCharsets.UTF_8).trim();
    } catch (UnsatisfiedLinkError ex) {
      throw new IOException("Motor de voz indisponível neste Android.", ex);
    }
  }
}
