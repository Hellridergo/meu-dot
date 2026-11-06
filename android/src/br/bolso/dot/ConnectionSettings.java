package br.bolso.dot;
import android.app.*;
import android.content.*;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.*;
import java.net.URI;

public final class ConnectionSettings extends Activity {
  Design d;EditText url,token;
  public void onCreate(Bundle state){super.onCreate(state);d=new Design(this);getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
    LinearLayout p=d.page();d.back(p,this::finish);d.title(p,"MEU DOT","Conexão","Vincule este celular à ponte do seu Dot.");
    LinearLayout panel=d.group(p,20);
    d.row(panel,NavIcon.OPEN,"Abrir painel da ponte","URL e token",this::openPanel);
    url=d.field(p,"Endereço da ponte","https://sua-ponte.chatgpt.site",false);url.setText(getSharedPreferences("config",0).getString("url",""));
    token=d.field(p,"Token do celular","Cole o token do painel",true);
    if(getSharedPreferences("config",0).contains("cipher"))token.setHint("Token salvo · digite para trocar");
    d.add(p,new android.view.View(this),8);
    d.button(p,"Salvar conexão",true,this::save);
    LinearLayout note=d.row();note.addView(new NavIcon(this,NavIcon.LOCK,Design.FAINT),new LinearLayout.LayoutParams(d.dp(16),d.dp(16)));
    TextView safe=d.text("Seu token é armazenado com a proteção do Android Keystore.",13,Design.MUTED);
    LinearLayout.LayoutParams sp=new LinearLayout.LayoutParams(0,-2,1);sp.leftMargin=d.dp(10);note.addView(safe,sp);d.add(p,note,18);
  }
  void openPanel(){
    String base=url==null?"":url.getText().toString().trim();
    if(!base.startsWith("https://")){android.widget.Toast.makeText(this,"Informe primeiro o endereço HTTPS da sua ponte.",android.widget.Toast.LENGTH_LONG).show();return;}
    while(base.endsWith("/"))base=base.substring(0,base.length()-1);
    startActivity(new Intent(Intent.ACTION_VIEW,android.net.Uri.parse(base+"/painel")));
  }
  void save(){try{
    String address=url.getText().toString().trim().replaceAll("/+$","");URI u=new URI(address);
    if(!"https".equals(u.getScheme())||u.getHost()==null||u.getUserInfo()!=null||u.getQuery()!=null||u.getFragment()!=null)throw new Exception();
    SharedPreferences p=getSharedPreferences("config",0);
    if(!address.equals(p.getString("url",""))&&(!p.getString("ack_id","").isEmpty()||!p.getString("receipt_id","").isEmpty())){
      Toast.makeText(this,"Há um recebimento ou confirmação aguardando envio. Mantenha a conexão atual até concluir.",Toast.LENGTH_LONG).show();return;}
    String secret=token.getText().toString().trim();if(secret.isEmpty())secret=SecureConfig.tokenFor(this,address);if(secret.length()<24)throw new Exception();
    SecureConfig.save(this,address,secret);token.setText("");Toast.makeText(this,"Conexão salva. Conecte a Waveshare na tela inicial.",Toast.LENGTH_LONG).show();finish();
    }catch(Exception e){Toast.makeText(this,"Confira a URL HTTPS e o token da ponte.",Toast.LENGTH_LONG).show();}}
}
