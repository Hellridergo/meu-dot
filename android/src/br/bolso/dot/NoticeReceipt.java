package br.bolso.dot;

/** Delivery is never a human answer. Old firmware remains manually dismissible. */
final class NoticeReceipt {
  final String id;
  final int tag;
  final boolean decision;
  boolean displayed, played;
  NoticeReceipt(String id, int tag, boolean decision) {
    this.id = id; this.tag = tag; this.decision = decision;
  }
  String stage(boolean sound) {
    if (decision || !displayed) return null;
    return played ? "played" : !sound ? "displayed" : null;
  }
}
