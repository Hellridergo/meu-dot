package br.bolso.dot;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.os.*;
import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

final class Radio extends BluetoothGattCallback {
  static final UUID SERVICE = id(0), RX = id(1), TX = id(2),
                    CCC =
                        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
  static UUID id(int sub) {
    return UUID.fromString(String.format(
        Locale.ROOT, "64f100%02x-5ac2-4d32-a6ea-2c940d0487ab", sub));
  }
  final Companion owner;
  final Handler h;
  BluetoothGatt g;
  BluetoothGattCharacteristic rx;
  BluetoothLeScanner scanner;
  boolean scanning, active, writing, closed;
  int mtu = 23;
  final ArrayDeque<byte[]> writes = new ArrayDeque<>();
  boolean streamCap, audioEvents, firstAudioInFlight, endAudioInFlight;
  byte[] controlInFlight;
  int writingCardTag;
  int audioTag;
  boolean audioIssued;
  Timings audioTrace, writeTrace, incomingTrace;
  // 35 bit 2 (firmware 0.4.2): speaker audio goes without response, TX also
  // notifies, and the microphone streams while it records.
  boolean fastLink, notifyOn, upgrading, liveClip;
  // Queued marker: rewrite the TX CCC to notifications + indications. Kept in
  // the write queue because Android allows one GATT operation at a time.
  static final byte[] NOTIFY_UPGRADE = new byte[0];
  static final byte[] BOTH_VALUE = {3, 0};
  // 0.4.4: pace the reply audio and survive a busy link. A flood of write
  // commands starved the board and delayed the indication confirmations it
  // waits for, which cut replies and dropped the connection.
  static final int GATT_CONGESTED = 0x8f;
  static final long LEAD_BYTES = 16000;  // 1 s of µ-law ahead of real time
  static final long BYTES_PER_MS = 40;   // then at most 2.5x real time
  long streamStart, streamSent;
  int writeRetries;
  byte[] inFlight;
  final Runnable repump = this::pump;
  static boolean isAudio(byte[] b) {
    return b != null && b.length > 0 && (b[0] & 255) >= 0x40 && (b[0] & 255) <= 0x47;
  }
  long writeCycle;
  final SpeechTransfer playback = new SpeechTransfer();
  int expected, sequence, clipId;
  ByteArrayOutputStream clip;
  final BroadcastReceiver bonds = new BroadcastReceiver() {
    public void onReceive(Context c, Intent i) {
      BluetoothDevice d = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
      if (g == null || d == null || !d.equals(g.getDevice()))
        return;
      if (d.getBondState() == BluetoothDevice.BOND_BONDED)
        g.discoverServices();
      else if (d.getBondState() == BluetoothDevice.BOND_NONE)
        lost("Pareamento interrompido.");
    }
  };
  Radio(Companion o) {
    owner = o;
    h = o.h;
    IntentFilter f =
        new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
    if (Build.VERSION.SDK_INT >= 33)
      o.registerReceiver(bonds, f, Context.RECEIVER_EXPORTED);
    else
      o.registerReceiver(bonds, f);
  }
  private final ScanCallback scan = new ScanCallback() {
    public void onScanResult(int t, ScanResult r) {
      h.post(() -> {
        if (!scanning || closed)
          return;
        stopScan();
        owner.report("Pareando. Confira o código na Waveshare.");
        g = r.getDevice().connectGatt(owner, false, Radio.this,
                                      BluetoothDevice.TRANSPORT_LE);
        BluetoothGatt pending = g;
        h.postDelayed(() -> {
          if (g == pending && !active)
            lost("Pareamento expirou.");
        }, 55000);
      });
    }
    public void onScanFailed(int code) {
      h.post(() -> {
        scanning = false;
        owner.report(
            "Não foi possível buscar a placa. Tente conectar novamente.");
      });
    }
  };
  void connect() {
    if (closed || scanning || g != null)
      return;
    BluetoothManager manager = owner.getSystemService(BluetoothManager.class);
    BluetoothAdapter a = manager == null ? null : manager.getAdapter();
    if (a == null || !a.isEnabled()) {
      owner.report("Ative o Bluetooth do celular.");
      return;
    }
    scanner = a.getBluetoothLeScanner();
    if (scanner == null)
      return;
    scanning = true;
    scanner.startScan(
        Collections.singletonList(new ScanFilter.Builder()
                                      .setServiceUuid(new ParcelUuid(SERVICE))
                                      .build()),
        new ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build(),
        scan);
    owner.report("Procurando a Waveshare por Bluetooth…");
    h.postDelayed(() -> {
      if (scanning) {
        stopScan();
        owner.report(
            "Placa não encontrada. Aproxime a Waveshare e toque em conectar.");
      }
    }, 20000);
  }
  void stopScan() {
    if (scanning && scanner != null) {
      scanning = false;
      scanner.stopScan(scan);
    }
  }
  void close() {
    closed = true;
    stopScan();
    owner.unregisterReceiver(bonds);
    lost("Desconectado.");
  }
  void lost(String msg) {
    android.util.Log.w("MeuDotBle", "conexão encerrada pelo app: " + msg);
    h.removeCallbacks(repump);
    streamStart = streamSent = 0;
    writeRetries = 0;
    inFlight = null;
    active = false;
    writing = false;
    writes.clear();
    playback.cancel();
    streamCap = false;
    audioEvents = false;
    audioIssued = false;
    controlInFlight = null;
    fastLink = false;
    notifyOn = false;
    upgrading = false;
    liveClip = false;
    firstAudioInFlight = false;
    endAudioInFlight = false;
    clip = null;
    rx = null;
    mtu = 23;
    BluetoothGatt old = g;
    g = null;
    if (old != null) {
      old.disconnect();
      old.close();
    }
    owner.disconnected(msg);
  }
  public void onConnectionStateChange(BluetoothGatt a, int s, int n) {
    h.post(() -> {
      if (a != g)
        return;
      if (n != BluetoothProfile.STATE_CONNECTED ||
          s != BluetoothGatt.GATT_SUCCESS) {
        // 8 supervision timeout, 19 ended by the board, 22 ended here.
        android.util.Log.w("MeuDotBle", "conexão caiu: status=" + s + " estado=" + n);
        lost("Bluetooth desconectado.");
        if (!closed)
          h.postDelayed(this::connect, 1800);
        return;
      }
      a.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
      // 2M PHY halves airtime per packet. The ESP32-S3 supports it; phones
      // without it keep 1M.
      a.setPreferredPhy(BluetoothDevice.PHY_LE_2M_MASK,
                        BluetoothDevice.PHY_LE_2M_MASK,
                        BluetoothDevice.PHY_OPTION_NO_PREFERRED);
      if (a.getDevice().getBondState() == BluetoothDevice.BOND_BONDED)
        a.discoverServices();
      else if (a.getDevice().getBondState() == BluetoothDevice.BOND_NONE)
        a.getDevice().createBond();
    });
  }
  public void onServicesDiscovered(BluetoothGatt a, int status) {
    h.post(() -> {
      if (g != a || active)
        return;
      BluetoothGattService service = a.getService(SERVICE);
      if (status != 0 || service == null) {
        lost("Firmware incompatível.");
        return;
      }
      rx = service.getCharacteristic(RX);
      if (rx == null || service.getCharacteristic(TX) == null) {
        lost("Serviço Bluetooth incompleto.");
        return;
      }
      if (!a.requestMtu(247))
        subscribe(a);
    });
  }
  public void onMtuChanged(BluetoothGatt a, int m, int status) {
    h.post(() -> {
      if (a == g && !active) {
        if (status == 0)
          mtu = m;
        subscribe(a);
      }
    });
  }
  void subscribe(BluetoothGatt a) {
    BluetoothGattCharacteristic tx =
        a.getService(SERVICE).getCharacteristic(TX);
    BluetoothGattDescriptor c = tx.getDescriptor(CCC);
    if (c == null || !a.setCharacteristicNotification(tx, true)) {
      lost("Falha ao assinar o áudio.");
      return;
    }
    c.setValue(BluetoothGattDescriptor.ENABLE_INDICATION_VALUE);
    if (!a.writeDescriptor(c))
      lost("Falha ao ativar o áudio.");
  }
  public void onDescriptorWrite(BluetoothGatt a, BluetoothGattDescriptor d,
                                int s) {
    h.post(() -> {
      if (a != g)
        return;
      if (upgrading) {
        // Without notifications the board keeps sending the microphone after
        // release, by indications. Nothing else changes.
        upgrading = false;
        writing = false;
        if (s == 0) {
          notifyOn = true;
          writes.add(new byte[] {8, 1});
        }
        pump();
        return;
      }
      if (s != 0) {
        lost("Pareamento não autorizado.");
        return;
      }
      active = true;
      // Bonding can renegotiate the interval; ask again once the link is up.
      a.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
      // 35 with the fast-link bit may arrive just before this callback.
      if (fastLink && !notifyOn && !writes.contains(NOTIFY_UPGRADE))
        writes.add(NOTIFY_UPGRADE);
      owner.connected();
      pump();
    });
  }
  void send(byte[] b) {
    if (!active)
      return;
    writes.add(b);
    pump();
  }
  void startAudio(VoiceBuffer buffer, Timings trace, int tag) {
    cancelAudio();
    streamStart = streamSent = 0;
    audioTrace = trace;
    audioTag = tag;
    audioIssued = true;
    if (audioEvents && trace != null)
      send(ByteBuffer.allocate(9).order(ByteOrder.LITTLE_ENDIAN)
          .put((byte)0x47).putInt(tag).putInt(trace.clipId).array());
    playback.begin(buffer);
    pump();
  }
  void cancelAudio() {
    streamStart = 0;
    boolean pending = playback.cancel();
    if ((pending || audioIssued) && active)
      writes.add(new byte[] {0x46});
    audioIssued = false;
    pump();
  }
  void pump() {
    if (writing || g == null || !active)
      return;
    h.removeCallbacks(repump);
    firstAudioInFlight = false;
    endAudioInFlight = false;
    byte[] b = writes.pollFirst();
    if (b == NOTIFY_UPGRADE) {
      controlInFlight = null;
      BluetoothGattService service = g.getService(SERVICE);
      BluetoothGattCharacteristic tx =
          service == null ? null : service.getCharacteristic(TX);
      BluetoothGattDescriptor c = tx == null ? null : tx.getDescriptor(CCC);
      if (c != null) {
        c.setValue(BOTH_VALUE);
        upgrading = writing = true;
        if (g.writeDescriptor(c))
          return;
        upgrading = writing = false;
      }
      pump();
      return;
    }
    controlInFlight = b;
    if (b != null && b.length == 8 && b[0] == 2) writingCardTag = le32(b, 2);
    if (b == null) {
      if (streamStart > 0 &&
          streamSent >= LEAD_BYTES + (SystemClock.elapsedRealtime() - streamStart) * BYTES_PER_MS) {
        h.postDelayed(repump, 10);
        return;
      }
      b = playback.next(streamCap, mtu);
      firstAudioInFlight = playback.firstPayload;
      writeCycle = playback.cycle;
      writeTrace = audioTrace;
      endAudioInFlight = b != null && (b[0] == 0x42 || b[0] == 0x45);
      if (b != null && b[0] == 0x43) {
        streamStart = SystemClock.elapsedRealtime();
        streamSent = 0;
      } else if (b != null && b[0] == 0x44)
        streamSent += b.length - 1;
      else if (b != null && b[0] == 0x45) {
        // 0.5.1: compare with the board's "voz: N bytes recebidos" line.
        android.util.Log.i("MeuDotBle", "voz enviada: " + streamSent + " bytes em "
            + (SystemClock.elapsedRealtime() - streamStart) + " ms");
        streamStart = 0;
      }
    }
    if (b == null)
      return;
    // Write commands need no response, so Android can place several in one
    // connection event. onCharacteristicWrite still paces them by the
    // stack's buffer credits. Control packets keep the acknowledged write.
    rx.setWriteType(fastLink && b[0] == 0x44
                        ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                        : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
    rx.setValue(b);
    writing = true;
    inFlight = b;
    if (!g.writeCharacteristic(rx)) {
      writing = false;
      if (isAudio(b) && ++writeRetries <= 150) {
        // Android refuses a write while its queue is full. Retry the same
        // packet shortly instead of dropping the link.
        writes.addFirst(b);
        h.postDelayed(repump, 10);
        return;
      }
      android.util.Log.w("MeuDotBle", "escrita recusada pelo Android, op=" + (b[0] & 255));
      lost("Escrita Bluetooth interrompida.");
    }
  }
  public void onCharacteristicWrite(BluetoothGatt a,
                                    BluetoothGattCharacteristic c, int status) {
    h.post(() -> {
      if (a != g)
        return;
      if (status != 0) {
        byte[] sent = inFlight;
        int op = sent == null || sent.length == 0 ? -1 : sent[0] & 255;
        writing = false;
        if (status == GATT_CONGESTED && isAudio(sent)) {
          // Accepted, but the link is busy: slow down instead of failing.
          android.util.Log.w("MeuDotBle", "link congestionado, op=" + op);
          h.postDelayed(repump, 20);
          return;
        }
        if (isAudio(sent)) {
          // The board closed this reply (for example after a long stall).
          // Drop its audio; the text card stays on screen.
          android.util.Log.w("MeuDotBle", "voz recusada pela placa: status=" + status + " op=" + op);
          playback.cancel();
          streamStart = 0;
          audioIssued = false;
          pump();
          return;
        }
        android.util.Log.w("MeuDotBle", "pacote recusado pela placa: status=" + status + " op=" + op);
        lost("A placa recusou o pacote Bluetooth.");
        return;
      }
      writeRetries = 0;
      if (endAudioInFlight && writeCycle == playback.cycle && !audioEvents)
        owner.releaseWake();
      if (firstAudioInFlight && writeCycle == playback.cycle)
        owner.trace(writeTrace, Timings.BLE);
      if (controlInFlight != null && controlInFlight[0] == 4)
        owner.cardDisplayed(writingCardTag);
      writing = false;
      pump();
    });
  }
  public void onCharacteristicChanged(BluetoothGatt a,
                                      BluetoothGattCharacteristic c) {
    if (Build.VERSION.SDK_INT < 33) {
      byte[] b = c.getValue().clone();
      h.post(() -> {
        if (a == g)
          packet(b);
      });
    }
  }
  public void onCharacteristicChanged(BluetoothGatt a,
                                      BluetoothGattCharacteristic c,
                                      byte[] value) {
    byte[] b = value.clone();
    h.post(() -> {
      if (g == a)
        packet(b);
    });
  }
  static int le32(byte[] b, int i) {
    return (b[i] & 255) | ((b[i + 1] & 255) << 8) | ((b[i + 2] & 255) << 16) |
        ((b[i + 3] & 255) << 24);
  }
  void packet(byte[] b) {
    if (b.length == 0)
      return;
    int op = b[0] & 255;
    if (op == 0x30 && b.length == 2 && b[1] == 1) {
      streamCap = true;
      pump();
      return;
    }
    if (op == 0x35 && b.length == 3 && b[1] == 1) {
      audioEvents = (b[2] & 1) != 0;
      if ((b[2] & 4) != 0) {
        // Decided by the announcement, not by the characteristic properties:
        // Android may keep a bonded board's old GATT table in its cache.
        fastLink = true;
        // Every fast-link board streams speaker audio (43/44/45). Do not
        // depend on 30 01: on a bonded reconnect it can reach Android before
        // service discovery and be dropped, which left replies on the legacy
        // framing that plays only after the whole clip arrives.
        streamCap = true;
        if (active && !notifyOn && !upgrading &&
            !writes.contains(NOTIFY_UPGRADE))
          writes.add(NOTIFY_UPGRADE);
        pump();
      }
      return;
    }
    if (op == 0x31 && b.length == 4 && b[1] == 1) {
      Companion.battery.update(b[2] & 255, b[3] & 255, Companion.clock());
      return;
    }
    if (op == 0x34 && b.length == 12) {
      // 0.5.2: the board's report on the reply it just played.
      int recv = (b[1] & 255) | (b[2] & 255) << 8, played = (b[3] & 255) | (b[4] & 255) << 8;
      int dryMs = (b[6] & 255) | (b[7] & 255) << 8, starved = (b[9] & 255) | (b[10] & 255) << 8;
      android.util.Log.i("MeuDotBle", "placa tocou " + played + " de " + recv + " ms recebidos; "
          + (b[5] & 255) + " falhas no alto-falante (" + dryMs + " ms), "
          + (b[8] & 255) + " pausas por falta de dados (" + starved + " ms), maior intervalo "
          + (b[11] & 255) + " ms");
      return;
    }
    if (op == 0x32 && b.length == 10 && audioEvents) {
      if (le32(b, 2) == audioTag && (b[1] == 2 || b[1] == 3)) audioIssued = false;
      owner.audioEvent(b[1] & 255, le32(b, 2), Integer.toUnsignedLong(le32(b, 6)));
      return;
    }
    if (op == 16 && b.length == 9) {
      liveClip = false;
      expected = le32(b, 1);
      clipId = le32(b, 5);
      incomingTrace = owner.beginReceipt(clipId);
      sequence = 0;
      clip = expected >= 4000 && expected <= PcmStream.LIMIT
                 ? new ByteArrayOutputStream(expected)
                 : null;
    } else if (op == 0x13 && b.length == 5) {
      // The board is still recording. The size is known only at 14, and the
      // trace starts there, so speaking time is not counted as transfer.
      owner.receiptStarting();
      liveClip = true;
      expected = PcmStream.LIMIT;
      clipId = le32(b, 1);
      sequence = 0;
      clip = new ByteArrayOutputStream(96000);
    } else if (op == 0x14 && b.length == 11) {
      int total = le32(b, 5), tail = (b[9] & 255) | ((b[10] & 255) << 8);
      if (clip != null && liveClip && le32(b, 1) == clipId &&
          clip.size() == total && total >= 4000) {
        // Measure from the release on the board: tail is how long the last
        // packets took to leave it.
        incomingTrace = owner.beginReceipt(clipId, Companion.clock() - tail);
        owner.upload(clip.toByteArray(), incomingTrace);
      } else
        owner.fail("Áudio incompleto. Grave novamente.");
      clip = null;
      liveClip = false;
    } else if (op == 0x16 && b.length == 5) {
      if (clip != null && liveClip && le32(b, 1) == clipId) {
        clip = null;
        liveClip = false;
        owner.releaseWake();
      }
    } else if (op == 17 && b.length > 3 && clip != null) {
      int seq = (b[1] & 255) | ((b[2] & 255) << 8);
      if (seq != sequence++ || clip.size() + b.length - 3 > expected) {
        clip = null;
        owner.fail("Áudio incompleto. Grave novamente.");
        return;
      }
      clip.write(b, 3, b.length - 3);
    } else if (op == 18 && b.length == 5) {
      if (clip != null && clip.size() == expected && le32(b, 1) == clipId)
        owner.upload(clip.toByteArray(), incomingTrace);
      else
        owner.fail("Áudio incompleto. Grave novamente.");
      clip = null;
    } else if (op == 33 && b.length == 6)
      owner.choice(b[1], le32(b, 2));
  }
  void parts(int op, byte[] data) {
    int part = Math.max(16, Math.min(240, mtu - 4));
    for (int i = 0; i < data.length; i += part) {
      int len = Math.min(part, data.length - i);
      byte[] b = new byte[len + 1];
      b[0] = (byte)op;
      System.arraycopy(data, i, b, 1, len);
      send(b);
    }
  }
  void card(int kind, int tag, String text) {
    byte[] payload = text.getBytes(StandardCharsets.UTF_8);
    if (payload.length == 0 || payload.length > 1098)
      return;
    send(ByteBuffer.allocate(8)
             .order(ByteOrder.LITTLE_ENDIAN)
             .put((byte)2)
             .put((byte)kind)
             .putInt(tag)
             .putShort((short)payload.length)
             .array());
    parts(3, payload);
    send(new byte[] {4});
  }
}
