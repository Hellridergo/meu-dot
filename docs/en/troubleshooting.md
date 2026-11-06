# Troubleshooting

Start with the symptom you see. Most problems are in one of three links: board ↔ phone (Bluetooth), phone ↔ bridge (internet) or bridge ↔ Dot (subscription).

## Installing

### The computer does not see the board

- Try another USB-C cable; many cables only charge.
- Force download mode:
  1. Unplug the board.
  2. Hold **BOOT**, plug the cable back in, then release BOOT.
- **Windows:** the port appears under *Ports (COM & LPT)*. No driver is needed for the ESP32-S3's native USB.
- **Linux:** add your user to `dialout` (`sudo usermod -aG dialout $USER`) and log in again.

### App not installed

Android shows only *"App not installed"*, without a reason, when:

- **the phone already has a newer version.** Android never installs an older version over a newer one. Check that you downloaded the right file: an old `0.1` APK is only 33 KB;
- **the installed app was signed with a different key**, for example one you built yourself with another `debug.p12`. Uninstall it first. This erases the app's pairing and keys;
- **Samsung's *Auto Blocker* is on.** Turn it off in *Settings → Security and privacy*.

With a computer and USB debugging, `adb install -r meu-dot-android-0.5.4.apk` prints the exact reason, for example `INSTALL_FAILED_VERSION_DOWNGRADE`.

## Pairing and connection

| Symptom | Check |
| --- | --- |
| The board stays on the pairing screen | The phone's Bluetooth is on. *Dispositivos próximos* (Nearby devices) is allowed for Meu Dot. The phone is close. Tap **Conectar Waveshare** again. |
| The code on the board does not match the phone | Cancel on the phone and try again. If the board was re-flashed with the full image, remove the old pairing from the phone's Bluetooth settings first. |
| *Dot offline* on the board | The phone has no internet, or the bridge address or token is wrong. Open *Ajustes → Conexão com seu Dot* and save again. |
| Connected, but the Dot never answers | In the bridge panel both subscriptions must show as confirmed. If not, send the panel instruction again in the Dot conversation and make sure the plugin is enabled there. |
| *Token recusado* (token refused) | Copy the token from the panel again. It must match `MOBILE_TOKEN` exactly. |

## Audio

| Symptom | Check |
| --- | --- |
| No voice, but the text appears | *Ouvir respostas na Waveshare* (play replies on the board) is on in the app. The board is not muted and its volume is above 0. For the Android voice, a Portuguese **offline** voice is installed. |
| The voice pauses in the middle | Other Bluetooth devices on the phone, such as smart glasses or earbuds, share the radio. The board pauses once to refill 1.5 s instead of cutting repeatedly. Keep the phone closer, or disconnect the other device. |
| Cloud voice fails | Check the key and your provider credits. The app falls back to the Android voice after an error or 8 s of silence. A Gemini *400* usually means the selected voice or model is not available on your key. |
| *O trecho foi curto* (too short) | Hold for at least a quarter of a second while you speak. |

## Reading the logs

- **Board** (USB connected):
  ```sh
  idf.py -p PORT monitor
  ```
  - `dot_ble` shows the reason for each disconnect. `0x208` is the radio timing out, `0x213` is the phone ending the link and `0x216` is the board ending it.
  - `dot_audio` shows pauses and speaker faults for each reply.
  - `dot_tempo` shows the time from release to the first sound.
- **Phone** (USB debugging):
  ```sh
  adb logcat -s MeuDotBle MeuDotTiming MeuDotVoz
  ```
  - The board's report on each reply appears as `MeuDotBle: placa tocou …`, so you can measure without a cable on the board.
  - The app's own timing breakdown is in *Ajustes → Diagnóstico de resposta*.

## Builds

- **"Unknown CMake command" or compiler failures with no message on macOS.** Check whether iCloud *Optimize Mac Storage* evicted ESP-IDF files. Run `ls -lO` on an ESP-IDF file and look for `dataless`. Keep the ESP-IDF folder downloaded (Finder → right click → *Keep Downloaded*), or install it outside iCloud-synced folders.
- **The APK does not install over an existing one.** Rebuild with the same `debug.p12` you used before.
