package br.bolso.dot;

final class BatteryInfo {
  private int flags, percent = 255;
  private long received;
  synchronized void update(int flags, int percent, long now) {
    this.flags = flags & 15;
    this.percent = (flags & 3) == 3 && percent >= 0 && percent <= 100 ? percent : 255;
    received = now;
  }
  synchronized void clear() { flags = 0; percent = 255; received = 0; }
  synchronized String label(long now) {
    if (received == 0 || now - received > 45000 || (flags & 1) == 0)
      return "Bateria indisponível";
    if ((flags & 2) == 0) return (flags & 8) != 0 ? "USB · sem bateria" : "Sem bateria";
    String value = percent <= 100 ? percent + "%" : "Percentual indisponível";
    return value + ((flags & 4) != 0 ? " · carregando" : (flags & 8) != 0 ? " · USB" : " · bateria");
  }
}
