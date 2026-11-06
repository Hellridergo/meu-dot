package br.bolso.dot;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

/** Device durations never subtract an Android clock from a server/board clock. */
final class Timings {
  static final int RX=0,AUDIO=1,STT_START=2,TEXT=3,UPLOAD_START=4,SENT=5,
      REPLY=6,TTS_START=7,SYNTH=8,BLE=9,SPEAKER=10,PLAYED=11,TTS_DONE=12;
  private static final String[] NAMES={"rx","audio_received","stt_started","text_ready",
      "upload_started","upload_done","reply_received","tts_started","first_pcm",
      "first_ble","first_audio","playback_done","tts_done"};
  final String recordingId;
  final int clipId;
  final String requestId,correlation;
  private final long[] at=new long[NAMES.length];
  private Timings source;
  private long boardElapsed=-1,boardEnd=-1,bridgeAge=-1;
  String input="",output="";
  Timings(){this("",0,0);}
  Timings(String id,int clip,long now){this(id,clip,now,"","");}
  private Timings(String id,int clip,long now,String request,String source){
    recordingId=id;clipId=clip;requestId=request;correlation=source;
    Arrays.fill(at,-1);at[RX]=now;
  }
  synchronized Timings forReply(String id,String source){
    refreshInput();
    Timings t=new Timings(recordingId,clipId,at[RX],id,source);
    System.arraycopy(at,0,t.at,0,SENT+1);t.input=input;
    t.source=this.source==null?this:this.source;return t;
  }
  private void refreshInput(){
    if(source!=null)synchronized(source){System.arraycopy(source.at,0,at,0,SENT+1);input=source.input;}
  }
  synchronized boolean mark(int stage,long time){
    if(stage<0||stage>=at.length||at[stage]>=0||recordingId.isEmpty()||time<at[RX])return false;
    at[stage]=time;return true;
  }
  synchronized void board(long ms){if(ms>0&&boardElapsed<0)boardElapsed=ms;}
  synchronized void boardDone(long ms){if(ms>0&&boardEnd<0)boardEnd=ms;}
  synchronized void bridgeAge(long created,long observed){
    if(created>0&&observed>=created&&observed-created<=3600000)bridgeAge=observed-created;
  }
  synchronized Map<String,Object> metrics(){
    refreshInput();
    Map<String,Object> result=new LinkedHashMap<>(),stages=new LinkedHashMap<>();
    for(int i=0;i<at.length;i++)if(at[i]>=at[RX]&&at[i]-at[RX]<=3600000)stages.put(NAMES[i],at[i]-at[RX]);
    result.put("stages_ms",stages);result.put("input",input);result.put("output",output);
    if(boardElapsed>=0&&boardElapsed<=3600000)result.put("board_first_ms",boardElapsed);
    if(boardEnd>=0&&boardEnd<=3600000)result.put("board_done_ms",boardEnd);
    if(bridgeAge>=0)result.put("bridge_card_age_ms",bridgeAge);
    return result;
  }
  synchronized Map<String,Object> export(){
    Map<String,Object> result=new LinkedHashMap<>();
    result.put("recording_id",recordingId);result.put("request_id",requestId);
    result.put("correlation",correlation);result.put("metrics",metrics());return result;
  }
  private String span(int a,int b){return at[a]>=0&&at[b]>=at[a]?(at[b]-at[a])+" ms":"—";}
  synchronized String summary(){
    refreshInput();
    if(recordingId.isEmpty())return "As medidas aparecem após uma gravação. Valores ausentes não são estimados.";
    return "Gravação "+recordingId+(requestId.isEmpty()?"":"\nResposta "+requestId+" · vínculo "+correlation)+
      "\nEntrada: "+input+" · Saída: "+output+
      "\n\nRecepção Bluetooth   "+span(RX,AUDIO)+
      "\nEspera pelo transcritor   "+span(AUDIO,STT_START)+
      "\nTranscrição   "+span(STT_START,TEXT)+
      "\nTexto → envio confirmado   "+span(TEXT,SENT)+
      "\nHTTP de envio   "+span(UPLOAD_START,SENT)+
      "\nPonte → resposta no celular   "+span(SENT,REPLY)+
      "\nInício do envio → resposta no celular   "+span(UPLOAD_START,REPLY)+
      "\nIdade da resposta ao consultar a ponte   "+(bridgeAge>=0?bridgeAge+" ms":"—")+
      "\nEspera para iniciar síntese   "+span(REPLY,TTS_START)+
      "\nSíntese → primeiro PCM   "+span(TTS_START,SYNTH)+
      "\nPrimeiro PCM → primeiro BLE aceito   "+span(SYNTH,BLE)+
      "\nBLE → primeiro áudio informado   "+span(BLE,SPEAKER)+
      "\nResposta → primeiro áudio informado   "+span(REPLY,SPEAKER)+
      "\nPrimeiro áudio → fim informado   "+span(SPEAKER,PLAYED)+
      "\nResposta → reprodução concluída   "+span(REPLY,PLAYED)+
      "\nSoltar → primeiro bloco no codec   "+(boardElapsed>=0?boardElapsed+" ms":"—")+
      "\nSoltar → fim no codec   "+(boardEnd>=0?boardEnd+" ms":"—")+
      "\n\nPonte → resposta inclui despacho, espera/geração do assistente, fila e consulta. Não separa geração do modelo. O codec informa escrita de áudio não silencioso; não é medição acústica. Os relógios da placa e do celular são independentes.";
  }
}
