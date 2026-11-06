package br.bolso.dot;
import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.util.*;
import org.json.*;

public final class Main extends Activity {
  Design d;FrameLayout root;LinearLayout body,nav;ScrollView scroll;
  TextView status,battery,last,modelStatus,bridge,link,voiceValue,linkValue;Button fetchModel,connect;
  LinearLayout connection,batteryChip;Design.Meter meter;DotView board;
  String requested;int page;boolean wasLinked;
  final Handler handler=new Handler(Looper.getMainLooper());
  final Runnable refresh=new Runnable(){public void run(){
    Design.update(status,Companion.status);
    Design.chipState(connection,Companion.linked?"Waveshare conectada":"Waveshare desconectada",Companion.linked?Design.MINT:Design.FAINT);
    String level=Companion.battery.label(Companion.clock());
    Design.update(battery,level);
    if(batteryChip!=null){String s=shortBattery(level);batteryChip.setVisibility(s.isEmpty()?View.GONE:View.VISIBLE);if(!s.isEmpty())Design.chipState(batteryChip,s,batteryColor(level));}
    if(meter!=null)meter.set(batteryFraction(level),batteryColor(level));
    Design.update(link,Companion.linked?"Conectada":"Desconectada");
    if(link!=null)link.setTextColor(Companion.linked?Design.MINT:Design.MUTED);
    Design.update(bridge,Companion.bridgeReady?"Disponível":"Indisponível");
    if(bridge!=null)bridge.setTextColor(Companion.bridgeReady?Design.MINT:Design.MUTED);
    if(board!=null)board.setGrey(!Companion.linked);
    Design.update(last,getSharedPreferences("config",0).getString("last_message","Sua próxima resposta aparece aqui."));
    if(modelStatus!=null)Design.update(modelStatus,OfflineVoice.state(Main.this));
    if(fetchModel!=null)fetchModel.setEnabled(!OfflineVoice.busy()&&!OfflineVoice.ready(Main.this));
    if(fetchModel!=null)fetchModel.setAlpha(fetchModel.isEnabled()?1f:.45f);
    if(connect!=null)connect.setVisibility(Companion.linked?View.GONE:View.VISIBLE);
    Design.update(voiceValue,voiceSummary());
    Design.update(linkValue,linkSummary());
    handler.postDelayed(this,1000);
  }};
  public void onCreate(Bundle state){super.onCreate(state);d=new Design(this);
    root=new FrameLayout(this);root.setBackgroundColor(Design.BG);d.insets(root);setContentView(root);
    scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);scroll.setVerticalScrollBarEnabled(false);
    root.addView(scroll,new FrameLayout.LayoutParams(-1,-1));
    body=d.column();body.setPadding(d.dp(20),d.dp(10),d.dp(20),d.dp(118));scroll.addView(body);
    nav=d.row();nav.setPadding(d.dp(6),d.dp(6),d.dp(6),d.dp(6));
    GradientDrawable bar=d.shape(Design.SURFACE2,26,true);nav.setBackground(bar);nav.setElevation(d.dp(14));
    FrameLayout.LayoutParams np=new FrameLayout.LayoutParams(-1,d.dp(70),Gravity.BOTTOM);np.setMargins(d.dp(16),0,d.dp(16),d.dp(14));
    root.addView(nav,np);
    page=state==null?0:state.getInt("page",0);render(page);
  }
  void render(int selected){page=selected;body.removeAllViews();nav.removeAllViews();scroll.scrollTo(0,0);
    status=battery=last=modelStatus=bridge=link=voiceValue=linkValue=null;fetchModel=connect=null;connection=batteryChip=null;meter=null;board=null;
    String[] names={"Início","Conversas","Dispositivo","Ajustes"};
    int[] icons={NavIcon.HOME,NavIcon.CHAT,NavIcon.DEVICE,NavIcon.SETTINGS};
    for(int i=0;i<4;i++){final int n=i;boolean on=page==i;LinearLayout tab=d.column();tab.setGravity(Gravity.CENTER);
      tab.setPadding(d.dp(2),d.dp(6),d.dp(2),d.dp(6));
      tab.setBackground(Design.ripple(on?d.shape(0x1ff99d22,20,false):null,d.shape(0xffffffff,20,false)));
      tab.addView(new NavIcon(this,icons[i],on?Design.AMBER:Design.MUTED),new LinearLayout.LayoutParams(d.dp(22),d.dp(22)));
      TextView label=d.text(names[i],11,on?Design.AMBER:Design.MUTED,d.medium);label.setGravity(Gravity.CENTER);d.add(tab,label,5);
      tab.setContentDescription(names[i]+(on?", selecionado":""));tab.setFocusable(true);tab.setClickable(true);tab.setOnClickListener(v->render(n));
      tab.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
      for(int c=0;c<tab.getChildCount();c++)tab.getChildAt(c).setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
      nav.addView(tab,new LinearLayout.LayoutParams(0,-1,1));
    }
    if(page==0)home();else if(page==1)conversations();else if(page==2)device();else settings();
    handler.removeCallbacks(refresh);handler.post(refresh);
  }
  void brand(){LinearLayout row=d.row();
    View glow=new View(this);GradientDrawable o=new GradientDrawable();o.setShape(GradientDrawable.OVAL);o.setColor(Design.AMBER);glow.setBackground(o);
    glow.setElevation(d.dp(4));glow.setOutlineSpotShadowColor(Design.AMBER);glow.setOutlineAmbientShadowColor(Design.AMBER);
    row.addView(glow,new LinearLayout.LayoutParams(d.dp(11),d.dp(11)));
    TextView name=d.text("Meu Dot",20,Design.INK,d.semibold);name.setLetterSpacing(-.02f);
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);lp.leftMargin=d.dp(9);row.addView(name,lp);
    LinearLayout.LayoutParams rp=new LinearLayout.LayoutParams(-1,d.dp(44));rp.topMargin=d.dp(4);body.addView(row,rp);
  }
  void home(){brand();
    TextView hi=d.text("Olá",34,Design.INK);d.add(body,hi,18);
    d.add(body,d.text("Seu Dot, sempre por perto.",15,Design.MUTED),4);
    FrameLayout hero=new FrameLayout(this);hero.setBackground(d.shape(Design.SURFACE,28,true));hero.setClipToOutline(true);
    hero.addView(new DotView(this),new FrameLayout.LayoutParams(-1,-1));
    connection=d.chip("Waveshare desconectada",Design.FAINT);
    FrameLayout.LayoutParams cl=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.START);cl.setMargins(d.dp(14),d.dp(14),0,0);hero.addView(connection,cl);
    batteryChip=d.chip("",Design.MINT);batteryChip.setVisibility(View.GONE);
    FrameLayout.LayoutParams bl=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.END);bl.setMargins(0,d.dp(14),d.dp(14),0);hero.addView(batteryChip,bl);
    status=d.text(Companion.status,14,Design.MUTED);status.setGravity(Gravity.CENTER);status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    FrameLayout.LayoutParams sl=new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM);sl.setMargins(d.dp(20),0,d.dp(20),d.dp(18));hero.addView(status,sl);
    LinearLayout.LayoutParams hp=new LinearLayout.LayoutParams(-1,d.dp(316));hp.topMargin=d.dp(20);body.addView(hero,hp);
    connect=d.button(body,"Conectar Waveshare",true,()->request("connect"));
    LinearLayout talk=d.row();talk.setPadding(d.dp(16),d.dp(14),d.dp(14),d.dp(14));
    talk.setBackground(Design.ripple(d.shape(Design.SURFACE,24,true),null));
    FrameLayout mic=new FrameLayout(this);mic.setBackground(d.gradient(Design.AMBER_HI,Design.AMBER,999));
    mic.setElevation(d.dp(6));mic.setOutlineSpotShadowColor(Design.AMBER);mic.setOutlineAmbientShadowColor(Design.AMBER);
    mic.addView(new NavIcon(this,NavIcon.MIC,Design.ON_AMBER),new FrameLayout.LayoutParams(d.dp(22),d.dp(22),Gravity.CENTER));
    talk.addView(mic,new LinearLayout.LayoutParams(d.dp(44),d.dp(44)));
    LinearLayout words=d.column();words.addView(d.text("Como falar com o Dot",16,Design.INK,d.medium));
    TextView how=d.text("Segure o personagem na placa e solte para enviar",13,Design.MUTED);d.add(words,how,2);
    LinearLayout.LayoutParams wp=new LinearLayout.LayoutParams(0,-2,1);wp.leftMargin=d.dp(14);talk.addView(words,wp);
    talk.addView(new NavIcon(this,NavIcon.CHEVRON,Design.FAINT),new LinearLayout.LayoutParams(d.dp(18),d.dp(18)));
    talk.setContentDescription("Como falar com o Dot na Waveshare");talk.setFocusable(true);talk.setClickable(true);talk.setOnClickListener(v->talkHelp());
    d.add(body,talk,12);
    LinearLayout card=d.card(body,12);d.eyebrow(card,"Última conversa",Design.MUTED,0);
    last=d.line(card,"",16,Design.INK,10);last.setMaxLines(3);last.setEllipsize(android.text.TextUtils.TruncateAt.END);last.setLineSpacing(d.dp(3),1f);
    TextView open=d.text("Abrir conversas  →",13.5f,Design.AMBER,d.medium);d.add(card,open,12);
    card.setBackground(Design.ripple(d.shape(Design.SURFACE,24,true),null));
    card.setContentDescription("Abrir última conversa");card.setClickable(true);card.setFocusable(true);card.setOnClickListener(v->render(1));
  }
  void talkHelp(){new AlertDialog.Builder(this).setTitle("Fale pela Waveshare")
      .setMessage("Segure o Dot na tela da placa, fale e solte para enviar. O arco âmbar mostra quanto dos 30 segundos você já usou. O Android faz a conexão com seu Dot.\n\nVocê também pode usar o botão BOOT se ele estiver configurado para falar.")
      .setPositiveButton("Entendi",null).show();}
  static String dayLabel(long at){
    Calendar c=Calendar.getInstance(),t=Calendar.getInstance();c.setTimeInMillis(at);
    int days=(int)((startOfDay(t)-startOfDay(c))/86400000L);
    if(days==0)return "HOJE";if(days==1)return "ONTEM";
    return new java.text.SimpleDateFormat("d 'de' MMMM",new Locale("pt","BR")).format(new Date(at)).toUpperCase(new Locale("pt","BR"));
  }
  static long startOfDay(Calendar c){Calendar x=(Calendar)c.clone();x.set(Calendar.HOUR_OF_DAY,0);x.set(Calendar.MINUTE,0);x.set(Calendar.SECOND,0);x.set(Calendar.MILLISECOND,0);return x.getTimeInMillis();}
  void conversations(){d.title(body,"MEU DOT","Conversas","Respostas recebidas neste celular. As decisões continuam sendo confirmadas na Waveshare.");
    try{JSONArray items=new JSONArray(getSharedPreferences("config",0).getString("history","[]"));
      if(items.length()==0){LinearLayout c=d.card(body,24);
        DotView dot=new DotView(this);c.addView(dot,new LinearLayout.LayoutParams(-1,d.dp(170)));
        TextView t=d.text("Sua primeira conversa começa na placa.",21,Design.INK,d.semibold);t.setGravity(Gravity.CENTER);d.add(c,t,4);
        TextView s=d.text("Segure o Dot, fale e solte. As próximas respostas ficam disponíveis aqui.",14.5f,Design.MUTED);s.setGravity(Gravity.CENTER);d.add(c,s,8);
        d.button(c,"Como falar",false,this::talkHelp);}
      String day="";
      for(int i=items.length()-1;i>=0;i--){JSONObject item=items.getJSONObject(i);long at=item.optLong("at");
        String label=dayLabel(at);if(!label.equals(day)){day=label;TextView sep=d.text(label,11,Design.FAINT,d.semibold);sep.setLetterSpacing(.16f);d.add(body,sep,22);}
        LinearLayout card=d.card(body,10);card.setPadding(d.dp(16),d.dp(14),d.dp(18),d.dp(16));
        LinearLayout head=d.row();head.addView(new DotView(this,DotView.AVATAR),new LinearLayout.LayoutParams(d.dp(30),d.dp(30)));
        String time=new java.text.SimpleDateFormat("HH:mm",new Locale("pt","BR")).format(new Date(at));
        TextView who=d.text("Dot  ·  "+time,13,Design.MUTED,d.medium);LinearLayout.LayoutParams wl=new LinearLayout.LayoutParams(-2,-2);wl.leftMargin=d.dp(10);head.addView(who,wl);
        card.addView(head);
        TextView text=d.line(card,item.optString("text"),16,Design.INK,10);text.setTextIsSelectable(true);text.setLineSpacing(d.dp(3),1f);
        if(item.optBoolean("decision")){LinearLayout badge=d.row();badge.setPadding(d.dp(10),d.dp(6),d.dp(12),d.dp(6));badge.setBackground(d.shape(Design.AMBER_WASH,999,false));
          View dot=new View(this);GradientDrawable o=new GradientDrawable();o.setShape(GradientDrawable.OVAL);o.setColor(Design.AMBER);dot.setBackground(o);badge.addView(dot,new LinearLayout.LayoutParams(d.dp(6),d.dp(6)));
          TextView b=d.text("Confirmação pedida na placa",12,Design.AMBER,d.semibold);LinearLayout.LayoutParams bp=new LinearLayout.LayoutParams(-2,-2);bp.leftMargin=d.dp(7);badge.addView(b,bp);
          LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-2,-2);lp.topMargin=d.dp(12);card.addView(badge,lp);}
      }
    }catch(JSONException ignored){d.line(body,"O histórico local está indisponível.",16,Design.MUTED,20);}
  }
  void device(){d.title(body,"MEU DOT","Seu dispositivo","Conexão e bateria da sua Waveshare.");
    LinearLayout card=d.card(body,20);card.setOrientation(LinearLayout.HORIZONTAL);card.setGravity(Gravity.CENTER_VERTICAL);card.setPadding(d.dp(16),d.dp(16),d.dp(16),d.dp(16));
    board=new DotView(this,DotView.MINI);card.addView(board,new LinearLayout.LayoutParams(d.dp(104),d.dp(104)));
    LinearLayout info=d.column();info.addView(d.text("Waveshare 1.75C",18,Design.INK,d.semibold));
    d.add(info,d.text("ESP32-S3 · AMOLED 1,75″",13,Design.MUTED),4);
    connection=d.chip("Waveshare desconectada",Design.FAINT);LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(-2,-2);cp.topMargin=d.dp(10);info.addView(connection,cp);
    LinearLayout.LayoutParams ip=new LinearLayout.LayoutParams(0,-2,1);ip.leftMargin=d.dp(18);card.addView(info,ip);
    status=d.line(body,"",14,Design.MUTED,14);status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    connect=d.button(body,"Conectar Waveshare",true,()->request("connect"));
    LinearLayout state=d.group(body,14);
    link=d.row(state,NavIcon.BLUETOOTH,"Bluetooth","",null);
    bridge=d.row(state,NavIcon.LINK,"Dot","",null);
    battery=d.row(state,NavIcon.BATTERY,"Bateria","",null);
    meter=new Design.Meter(this);LinearLayout.LayoutParams mp=new LinearLayout.LayoutParams(d.dp(54),d.dp(6));mp.leftMargin=d.dp(10);((LinearLayout)battery.getParent()).addView(meter,mp);
    LinearLayout actions=d.group(body,14);
    d.row(actions,NavIcon.PLAY,Design.INK,"Testar personagem e som",Design.INK,"",()->request("test"));
    d.row(actions,NavIcon.LINK,Design.INK,"Configurar conexão",Design.INK,"",()->startActivity(new Intent(this,ConnectionSettings.class)));
    d.row(actions,NavIcon.CLOCK,Design.INK,"Tempos da última resposta",Design.INK,"",this::diagnostics);
    d.row(actions,NavIcon.POWER,Design.CORAL,"Desconectar",Design.CORAL,"",()->stopService(new Intent(this,Companion.class)));
    d.line(body,"A bateria é lida na placa. Sem telemetria recente, o indicador fica indisponível.",13,Design.FAINT,16);
  }
  String voiceSummary(){SharedPreferences v=getSharedPreferences("voice",0);
    String in=v.getString("stt","local").equals("google")?"Gemini":"Whisper";
    String out=v.getString("tts","android");out=out.equals("fish")?"Fish":out.equals("google")?"Gemini":"Android";
    return in+" · "+out;}
  String linkSummary(){return getSharedPreferences("config",0).contains("cipher")?"Configurada":"Configurar";}
  void settings(){d.title(body,"MEU DOT","Do seu jeito","Escolha como ouvir e mantenha sua conexão pronta.");
    LinearLayout main=d.group(body,20);
    voiceValue=d.row(main,NavIcon.WAVE,"Voz e provedores",voiceSummary(),()->startActivity(new Intent(this,VoiceSettings.class)));
    linkValue=d.row(main,NavIcon.LINK,"Conexão com seu Dot",linkSummary(),()->startActivity(new Intent(this,ConnectionSettings.class)));
    d.switchRow(main,NavIcon.PLAY,"Ouvir respostas na Waveshare",getSharedPreferences("config",0).getBoolean("sound",true),
        (b,v)->getSharedPreferences("config",0).edit().putBoolean("sound",v).apply());
    LinearLayout local=d.card(body,14);LinearLayout head=d.row();
    head.addView(d.iconTile(NavIcon.CHIP,Design.AMBER),new LinearLayout.LayoutParams(d.dp(34),d.dp(34)));
    LinearLayout words=d.column();words.addView(d.text("Reconhecimento local",16,Design.INK,d.medium));
    modelStatus=d.text(OfflineVoice.state(this),13,Design.MUTED);d.add(words,modelStatus,3);
    LinearLayout.LayoutParams wp=new LinearLayout.LayoutParams(0,-2,1);wp.leftMargin=d.dp(14);head.addView(words,wp);local.addView(head);
    fetchModel=d.button(local,"Baixar modelo · 60 MB",false,()->OfflineVoice.prepare(this));
    LinearLayout more=d.group(body,14);
    d.row(more,NavIcon.CLOCK,Design.INK,"Diagnóstico de resposta",Design.INK,"",this::diagnostics);
    String version="";try{version=" · "+getPackageManager().getPackageInfo(getPackageName(),0).versionName;}catch(Exception ignored){}
    d.line(body,"Meu Dot"+version+"\nProjeto independente, sem vínculo com a OpenAI ou a Waveshare.",12,Design.FAINT,22);
  }
  static String shortBattery(String label){
    java.util.regex.Matcher m=java.util.regex.Pattern.compile("^(\\d+%)").matcher(label);
    if(m.find())return m.group(1)+(label.contains("carregando")?" · carregando":"");
    return label.startsWith("USB")?"USB":"";
  }
  static float batteryFraction(String label){
    java.util.regex.Matcher m=java.util.regex.Pattern.compile("^(\\d+)%").matcher(label);
    return m.find()?Integer.parseInt(m.group(1))/100f:-1;
  }
  static int batteryColor(String label){float f=batteryFraction(label);
    return label.contains("carregando")?Design.MINT:f>=0&&f<=.15f?Design.CORAL:Design.MINT;}
  void diagnostics(){ScrollView s=new ScrollView(this);TextView t=d.text("Última consulta à ponte: "+(Companion.lastPollMs>0?Companion.lastPollMs+" ms":"—")+"\n\n"+
      (Companion.timing.recordingId.isEmpty()?getSharedPreferences("config",0).getString("last_trace",Companion.timing.summary()):Companion.timing.summary()),14,Design.INK);
    t.setTextIsSelectable(true);t.setPadding(d.dp(22),d.dp(16),d.dp(22),d.dp(20));s.addView(t);
    new AlertDialog.Builder(this).setTitle("Tempos medidos").setView(s).setPositiveButton("Fechar",null).show();
  }
  void toast(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}
  void request(String action){requested=action;ArrayList<String> list=new ArrayList<>();
    for(String perm:new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT})if(checkSelfPermission(perm)!=PackageManager.PERMISSION_GRANTED)list.add(perm);
    if(!list.isEmpty()){requestPermissions(list.toArray(new String[0]),12);return;}
    startForegroundService(new Intent(this,Companion.class).setAction(action));
  }
  public void onRequestPermissionsResult(int r,String[] p,int[] result){super.onRequestPermissionsResult(r,p,result);if(r==12){if(result.length==0)return;for(int x:result)if(x!=PackageManager.PERMISSION_GRANTED){toast("Libere Dispositivos próximos para parear.");return;}request(requested);}}
  protected void onSaveInstanceState(Bundle b){b.putInt("page",page);super.onSaveInstanceState(b);}
  protected void onResume(){super.onResume();handler.removeCallbacks(refresh);handler.post(refresh);}
  protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
}
