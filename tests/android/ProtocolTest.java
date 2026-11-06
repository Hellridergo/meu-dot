package br.bolso.dot;
public class ProtocolTest {
 static void check(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] args){
  NoticeReceipt notice=new NoticeReceipt("n",7,false);
  check(notice.stage(false)==null);notice.displayed=true;
  check(notice.stage(true)==null);check("displayed".equals(notice.stage(false)));
  notice.played=true;check("played".equals(notice.stage(true)));
  NoticeReceipt decision=new NoticeReceipt("d",8,true);decision.displayed=decision.played=true;
  check(decision.stage(false)==null&&decision.stage(true)==null);
  BatteryInfo b=new BatteryInfo();check(b.label(1).contains("indisponível"));
  b.update(15,100,1000);check(b.label(1100).equals("100% · carregando"));
  b.update(3,0,1000);check(b.label(1100).equals("0% · bateria"));
  b.update(9,88,1000);check(b.label(1100).equals("USB · sem bateria"));
  b.update(3,254,1000);check(b.label(1100).startsWith("Percentual indisponível"));
  check(b.label(47000).equals("Bateria indisponível"));
  b.clear();check(b.label(1200).equals("Bateria indisponível"));
  Timings old=new Timings("old-recording",15,1000),fresh=new Timings("new-recording",16,2000);
  check(old.mark(Timings.AUDIO,1100));check(!old.mark(Timings.AUDIO,1500));
  old.mark(Timings.TEXT,1200);old.mark(Timings.SENT,1300);old.mark(Timings.REPLY,265039);
  old.board(264039);check(old.summary().contains("263739 ms"));check(old.summary().contains("264039 ms"));
  check(!fresh.summary().contains("263739 ms"));check(fresh.summary().contains("new-recording"));
  System.out.println("Notice/decision safety, zero/absent/stale battery, idempotent stages and late correlated responses passed");
 }
}
