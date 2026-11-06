package br.bolso.dot;
import java.util.*;
public class LatencyTest {
  static void check(boolean ok){if(!ok)throw new AssertionError();}
  public static void main(String[] args){
    String a="f0d264de-a8f6-499a-a031-79aa6480799f",b="8c1b6909-21fd-43ff-8fc4-831b8ee7a7e9";
    Map<String,Timings> inputs=new LinkedHashMap<>();
    Timings original=new Timings(a,1,0),latest=new Timings(b,2,1000);
    inputs.put(a,original);inputs.put(b,latest);
    original.mark(Timings.AUDIO,300);original.mark(Timings.STT_START,500);
    original.mark(Timings.TEXT,1500);original.mark(Timings.UPLOAD_START,1510);original.mark(Timings.SENT,1700);
    Timings reply=TraceCorrelation.reply(inputs,"","resposta-"+a);
    check(reply!=null&&reply.recordingId.equals(a)&&reply!=original);
    check(TraceCorrelation.reply(inputs,b,"resposta-"+a)==null);
    check(TraceCorrelation.reply(inputs,"","unrelated-notice")==null);
    check(TraceCorrelation.reply(inputs,"unknown","resposta-"+a)==null);
    reply.mark(Timings.REPLY,21000);reply.mark(Timings.TTS_START,21020);
    reply.mark(Timings.SYNTH,21500);reply.mark(Timings.BLE,21600);
    reply.mark(Timings.SPEAKER,22000);reply.board(21850);
    reply.mark(Timings.PLAYED,26000);reply.boardDone(25850);
    check(!reply.mark(Timings.SPEAKER,23000));check(!reply.mark(Timings.TTS_DONE,-1));
    reply.bridgeAge(1793752173332L,1793752173542L);
    Map<?,?> metrics=reply.metrics(),stages=(Map<?,?>)metrics.get("stages_ms");
    check(stages.get("rx").equals(0L)&&stages.get("first_audio").equals(22000L));
    check(metrics.get("board_first_ms").equals(21850L)&&metrics.get("bridge_card_age_ms").equals(210L));
    check(!stages.containsKey("tts_done"));check(reply.summary().contains("1000 ms"));
    check(reply.export().get("request_id").equals("resposta-"+a));
    Timings other=TraceCorrelation.reply(inputs,a,"another-reply");
    check(!((Map<?,?>)other.metrics().get("stages_ms")).containsKey("first_audio"));
    other.mark(Timings.REPLY,265000);check(other.summary().contains("263300 ms"));
    check(PollSchedule.delay(-1)==6000&&PollSchedule.delay(12000)==550&&PollSchedule.delay(44999)==550);
    check(PollSchedule.delay(45000)==2500&&PollSchedule.delay(300000)==6000);
    Timings overlap=new Timings(a,3,1000),early=overlap.forReply("early","recording_id");
    early.mark(Timings.REPLY,1100);overlap.mark(Timings.SENT,1200);
    check(((Map<?,?>)early.metrics().get("stages_ms")).get("upload_done").equals(200L));
    check(!early.summary().contains("-100 ms"));
    System.out.println("PASS recording correlation, independent replies, monotonic first/end audio, missing stages and bounded poll schedule");
  }
}
