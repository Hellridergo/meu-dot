package br.bolso.dot;
import android.content.*;
import android.security.keystore.*;
import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;

final class SecureConfig {
  private static synchronized SecretKey key() throws Exception {
    KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
    ks.load(null);
    if (!ks.containsAlias("dot-bolso")) {
      KeyGenerator generator =
          KeyGenerator.getInstance("AES", "AndroidKeyStore");
      generator.init(
          new KeyGenParameterSpec
              .Builder("dot-bolso", KeyProperties.PURPOSE_ENCRYPT |
                                        KeyProperties.PURPOSE_DECRYPT)
              .setBlockModes("GCM")
              .setEncryptionPaddings("NoPadding")
              .build());
      return generator.generateKey();
    }
    return (SecretKey)ks.getKey("dot-bolso", null);
  }
  static synchronized void save(Context c, String url, String token)
      throws Exception {
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    cipher.init(Cipher.ENCRYPT_MODE, key());
    cipher.updateAAD(url.getBytes(StandardCharsets.UTF_8));
    byte[] secret = cipher.doFinal(token.getBytes(StandardCharsets.UTF_8));
    SharedPreferences p = c.getSharedPreferences("config", 0);
    SharedPreferences.Editor edit = p.edit();
    if (!url.equals(p.getString("url", "")))
      edit.remove("ack_id").remove("ack_option");
    boolean stored =
        edit.putString("url", url)
            .putBoolean("url_bound", true)
            .putString("iv",
                       Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP))
            .putString("cipher", Base64.encodeToString(secret, Base64.NO_WRAP))
            .commit();
    if (!stored)
      throw new java.io.IOException("Falha ao guardar a conexão.");
  }
  static synchronized String tokenFor(Context c, String base) throws Exception {
    SharedPreferences p = c.getSharedPreferences("config", 0);
    if (!base.equals(p.getString("url", "")))
      throw new java.io.IOException("A ponte mudou.");
    String data = p.getString("cipher", "");
    if (data.isEmpty())
      return "";
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    cipher.init(
        Cipher.DECRYPT_MODE, key(),
        new GCMParameterSpec(128, Base64.decode(p.getString("iv", ""), 0)));
    if (p.getBoolean("url_bound", false))
      cipher.updateAAD(base.getBytes(StandardCharsets.UTF_8));
    return new String(cipher.doFinal(Base64.decode(data, 0)),
                      StandardCharsets.UTF_8);
  }

  private static String providerTag(String provider) {
    if (!("google".equals(provider) || "fish".equals(provider)))
      throw new IllegalArgumentException("Provedor desconhecido");
    return "key_" + provider;
  }
  static boolean hasProvider(Context c, String provider) {
    return c.getSharedPreferences("voice", 0).contains(providerTag(provider));
  }
  static void putProvider(SharedPreferences.Editor edit, String provider,
                          String value) throws Exception {
    String tag = providerTag(provider);
    if (value.isEmpty()) {
      edit.remove(tag).remove(tag + "_iv");
      return;
    }
    if (value.length() > 4096 || !value.matches("[!-~]+"))
      throw new java.io.IOException("Formato de chave inválido.");
    Cipher g = Cipher.getInstance("AES/GCM/NoPadding");
    g.init(Cipher.ENCRYPT_MODE, key());
    g.updateAAD(tag.getBytes(StandardCharsets.UTF_8));
    edit.putString(tag, Base64.encodeToString(
                            g.doFinal(value.getBytes(StandardCharsets.UTF_8)),
                            Base64.NO_WRAP));
    edit.putString(tag + "_iv",
                   Base64.encodeToString(g.getIV(), Base64.NO_WRAP));
  }
  static String provider(Context c, String provider)
      throws java.io.IOException {
    try {
      String tag = providerTag(provider);
      java.util.Map<String, ?> values =
          c.getSharedPreferences("voice", 0).getAll();
      if (!values.containsKey(tag))
        throw new java.io.IOException(
            "Informe a chave do provedor em Configurações de voz.");
      Cipher g = Cipher.getInstance("AES/GCM/NoPadding");
      g.init(Cipher.DECRYPT_MODE, key(),
             new GCMParameterSpec(
                 128, Base64.decode((String)values.get(tag + "_iv"), 0)));
      g.updateAAD(tag.getBytes(StandardCharsets.UTF_8));
      return new String(g.doFinal(Base64.decode((String)values.get(tag), 0)),
                        StandardCharsets.UTF_8);
    } catch (java.io.IOException e) {
      throw e;
    } catch (Exception e) {
      throw new java.io.IOException("Salve novamente a chave deste provedor.");
    }
  }
}
