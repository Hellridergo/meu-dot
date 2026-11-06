package br.bolso.dot;

import android.app.*;
import android.content.*;
import android.graphics.Typeface;
import android.os.*;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import java.util.*;

public final class VoiceSettings extends Activity {
  static final String[] VOICES = {
      "Zephyr",        "Puck",         "Charon",     "Kore",        "Fenrir",
      "Leda",          "Orus",         "Aoede",      "Callirrhoe",  "Autonoe",
      "Enceladus",     "Iapetus",      "Umbriel",    "Algieba",     "Despina",
      "Erinome",       "Algenib",      "Rasalgethi", "Laomedeia",   "Achernar",
      "Alnilam",       "Schedar",      "Gacrux",     "Pulcherrima", "Achird",
      "Zubenelgenubi", "Vindemiatrix", "Sadachbia",  "Sadaltager",  "Sulafat"};
  Design design;
  LinearLayout googlePanel, fishPanel;
  SharedPreferences pref;
  LinearLayout panel;
  Spinner input, output, voice;
  EditText fishId, googleKey, fishKey;
  TextView diag;
  final Handler h = new Handler(Looper.getMainLooper());
  final Runnable refresh = new Runnable() {
    public void run() {
      Design.update(diag, Companion.timing.recordingId.isEmpty() ? getSharedPreferences("config", 0).getString("last_trace", Companion.timing.summary()) : Companion.timing.summary());
      h.postDelayed(this, 1000);
    }
  };
  int dp(int x) {
    return Math.round(getResources().getDisplayMetrics().density * x);
  }
  TextView text(String label, int size, int margin) {
    TextView t = design.line(panel, label, size, size >= 16 ? Design.INK : Design.MUTED, margin);
    if (size >= 16)
      t.setTypeface(design.medium);
    return t;
  }
  Spinner select(String title, String[] choices, int position) {
    text(title, 16, 14);
    Spinner s = new Spinner(this);
    ArrayAdapter<String> a =
        new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, choices);
    a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
    s.setAdapter(a);
    s.setSelection(Math.max(0, position));
    s.setMinimumHeight(dp(54));
    s.setBackground(design.select());
    s.setPadding(dp(6), 0, dp(40), 0);
    s.setPopupBackgroundDrawable(design.shape(Design.SURFACE2, 16, true));
    design.add(panel, s, 8);
    return s;
  }
  EditText field(String title, boolean secret) {
    return design.field(panel, title, secret ? "Cole sua chave" : "", secret);
  }
  void button(String label, Runnable run) {
    design.button(panel, label, label.equals("Salvar opções"), run);
  }
  void message(String m) { Toast.makeText(this, m, Toast.LENGTH_LONG).show(); }
  void hint(EditText e, String provider) {
    e.setHint(SecureConfig.hasProvider(this, provider)
                  ? "Chave salva · digite para trocar"
                  : "Cole sua chave");
  }
  void remove(String provider, EditText e) {
    new AlertDialog.Builder(this)
        .setTitle("Remover esta chave?")
        .setMessage("A opção em uso por este provedor volta para o modo local.")
        .setNegativeButton("Voltar", null)
        .setPositiveButton(
            "Remover",
            (dlg, which) -> {
              try {
                SharedPreferences.Editor edit = pref.edit();
                SecureConfig.putProvider(edit, provider, "");
                if (provider.equals(pref.getString("stt", "local")))
                  edit.putString("stt", "local");
                if (provider.equals(pref.getString("tts", "android")))
                  edit.putString("tts", "android");
                if (!edit.commit())
                  throw new Exception();
                e.setText("");
                hint(e, provider);
                if (provider.equals("google") &&
                    input.getSelectedItemPosition() == 1)
                  input.setSelection(0);
                if ((provider.equals("google") &&
                     output.getSelectedItemPosition() == 1) ||
                    (provider.equals("fish") &&
                     output.getSelectedItemPosition() == 2))
                  output.setSelection(0);
                message("Chave removida.");
              } catch (Exception ex) {
                message("Não consegui remover a chave.");
              }
            })
        .show();
  }
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
    pref = getSharedPreferences("voice", 0);
    design = new Design(this);
    panel = design.page();
    design.back(panel, this::finish);
    design.title(panel, "MEU DOT", "Sua voz, seu jeito", "Escolha onde reconhecer sua fala e como ouvir as respostas.");
    input = select(
        "Reconhecimento da sua fala",
        new String[] {"Whisper · no celular", "Google · Gemini 3.5 Transcribe"},
        pref.getString("stt", "local").equals("google") ? 1 : 0);
    String o = pref.getString("tts", "android");
    output = select("Leitura da resposta",
                    new String[] {"Android · síntese local",
                                  "Google · Gemini 3.8 Flash-Lite TTS",
                                  "Fish Audio · Realtime WebSocket"},
                    o.equals("fish")     ? 2
                    : o.equals("google") ? 1
                                         : 0);
    LinearLayout root = panel;
    googlePanel = design.card(root, 20);
    panel = googlePanel;
    voice = select(
        "Voz do Gemini", VOICES,
        Arrays.asList(VOICES).indexOf(pref.getString("gemini_voice", "Kore")));
    googleKey = field("Chave da sua conta Google Gemini", true);
    hint(googleKey, "google");
    button("Remover chave Google", () -> remove("google", googleKey));
    fishPanel = design.card(root, 20);
    panel = fishPanel;
    fishId = field("ID da voz Fish Audio", false);
    fishId.setHint("32 caracteres hexadecimais");
    fishId.setText(pref.getString("fish_id", ""));
    fishKey = field("Chave da sua conta Fish Audio", true);
    hint(fishKey, "fish");
    button("Remover chave Fish", () -> remove("fish", fishKey));
    panel = root;
    AdapterView.OnItemSelectedListener providers = new AdapterView.OnItemSelectedListener() {
      public void onNothingSelected(AdapterView<?> p) {}
      public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
        googlePanel.setVisibility(input.getSelectedItemPosition() == 1 || output.getSelectedItemPosition() == 1 ? View.VISIBLE : View.GONE);
        fishPanel.setVisibility(output.getSelectedItemPosition() == 2 ? View.VISIBLE : View.GONE);
      }
    };
    input.setOnItemSelectedListener(providers);
    output.setOnItemSelectedListener(providers);
    providers.onItemSelected(null, null, 0, 0);
    text("Google recebe a gravação quando escolhido para reconhecer sua "
             + "fala. Google ou Fish recebem o texto da resposta quando " +
               "escolhidos "
             + "para leitura. O uso segue a cobrança da sua conta. As chaves " +
               "ficam "
             + "cifradas pelo Android Keystore.",
         14, 15);
    button("Salvar opções", this::save);
    text("Último comando · medidas no celular", 17, 20);
    diag = text("", 14, 7);
    button("Copiar medições",()->{
      String json=getSharedPreferences("config",0).getString("last_trace_json","");
      if(json.isEmpty()){message("Ainda não há medições registradas.");return;}
      getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("Medições Meu Dot",json));
      message("Medições copiadas. O registro não contém transcrição, resposta ou chaves.");
    });
    button("Voltar", this::finish);
  }
  void save() {
    String stt = input.getSelectedItemPosition() == 1 ? "google" : "local";
    String tts = output.getSelectedItemPosition() == 2   ? "fish"
                 : output.getSelectedItemPosition() == 1 ? "google"
                                                         : "android";
    String g = googleKey.getText().toString().trim(),
           f = fishKey.getText().toString().trim(),
           id = fishId.getText().toString().trim().toLowerCase(Locale.ROOT);
    if ((stt.equals("google") || tts.equals("google")) && g.isEmpty() &&
        !SecureConfig.hasProvider(this, "google")) {
      message("Informe a chave de API do Google.");
      return;
    }
    if (tts.equals("fish") &&
        (f.isEmpty() && !SecureConfig.hasProvider(this, "fish") ||
         !id.matches("[a-f0-9]{32}"))) {
      message("Informe a chave Fish e o ID da voz com 32 caracteres.");
      return;
    }
    Runnable commit = () -> {
      try {
        SharedPreferences.Editor e = pref.edit();
        if (!g.isEmpty())
          SecureConfig.putProvider(e, "google", g);
        if (!f.isEmpty())
          SecureConfig.putProvider(e, "fish", f);
        if (!e.putString("stt", stt)
                 .putString("tts", tts)
                 .putString("gemini_voice", (String)voice.getSelectedItem())
                 .putString("fish_id", id)
                 .putBoolean("consent",
                             !stt.equals("local") || !tts.equals("android"))
                 .commit())
          throw new Exception();
        googleKey.setText("");
        fishKey.setText("");
        hint(googleKey, "google");
        hint(fishKey, "fish");
        message("Opções salvas.");
        finish();
      } catch (Exception ex) {
        message("Confira as chaves e tente salvar novamente.");
      }
    };
    if (stt.equals("local") && tts.equals("android")) {
      commit.run();
      return;
    }
    String info =
        (stt.equals("google")
             ? "Sua gravação será enviada à API Gemini do Google. "
             : "") +
        (tts.equals("google")
             ? "O texto das respostas será enviado ao Google para síntese. "
         : tts.equals("fish") ? "O texto das respostas e o ID de voz serão "
                                    + "enviados à Fish Audio. "
                              : "") +
        "Podem ocorrer cobranças na conta ligada à sua chave.";
    new AlertDialog.Builder(this)
        .setTitle("Usar voz em nuvem?")
        .setMessage(info)
        .setNegativeButton("Voltar", null)
        .setPositiveButton("Autorizar e salvar", (d, w) -> commit.run())
        .show();
  }
  protected void onResume() {
    super.onResume();
    h.post(refresh);
  }
  protected void onPause() {
    h.removeCallbacks(refresh);
    super.onPause();
  }
}
