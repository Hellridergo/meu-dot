package br.bolso.dot;
final class PollSchedule {
  static long delay(long age){
    if(age<0)return 6000;
    if(age<45000)return 550;
    return age<300000?2500:6000;
  }
}
