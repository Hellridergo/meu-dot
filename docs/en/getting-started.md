# Getting started

This guide takes you from a boxed board to your first spoken answer. It assumes no experience with ESP32 or Android development. Plan for about an hour, most of it spent setting up the bridge.

> [!TIP]
> The board and the app are in Portuguese. The exact labels you will see are quoted in *italics*, with a translation the first time.

**Contents**

1. [Get the hardware](#1-get-the-hardware)
2. [Install the firmware on the board](#2-install-the-firmware-on-the-board)
3. [Install the app on your phone](#3-install-the-app-on-your-phone)
4. [Deploy your bridge](#4-deploy-your-bridge)
5. [Connect your Dot to the bridge](#5-connect-your-dot-to-the-bridge)
6. [Connect the app to the bridge](#6-connect-the-app-to-the-bridge)
7. [Choose how to hear and be heard](#7-choose-how-to-hear-and-be-heard)
8. [Pair the board with the phone](#8-pair-the-board-with-the-phone)
9. [Your first conversation](#9-your-first-conversation)
10. [Everyday use](#10-everyday-use)
11. [Updating](#11-updating)

## 1. Get the hardware

| You need | Why |
| --- | --- |
| **Waveshare ESP32-S3-Touch-AMOLED-1.75C** | The device itself. It is sold with a battery (SKU 33691) or without one (SKU 33692), and comes in an aluminium case with room for the battery. |
| 3.7 V Li-ion battery with an **MX1.25 2-pin** plug | Only if you bought the board without one. Without a battery the board still works on USB power. |
| USB-C **data** cable | Some cables only charge. If the computer never sees the board, try another cable. |
| Android phone | Android 12 or newer, ARM64 (almost every phone from 2019 on), with Bluetooth. |
| Computer | Windows, macOS or Linux, to install the firmware once. |
| ChatGPT account | With access to **Dot conversations**, **custom MCP plugins** and **ChatGPT Sites**. |

> [!WARNING]
> If you connect a battery yourself, check the polarity printed next to the connector before plugging it in. A reversed battery can destroy the board.

## 2. Install the firmware on the board

Pick one of the two ways. The web installer needs nothing installed on your computer.

### Option A: web installer (recommended)

1. Open **https://hellridergo.github.io/meu-dot/flasher/** in **Chrome** or **Edge** on a computer. Safari and Firefox do not support Web Serial.
2. Connect the board to the computer with the USB-C cable.
3. Click **Install Meu Dot** and pick the port that appears. It is usually called *USB JTAG/serial debug unit*.
4. Confirm the installation and wait about a minute without unplugging the board.
5. When it finishes, the board restarts and shows the amber Dot.

If no port appears:

1. Unplug the board.
2. Hold the **BOOT** button.
3. Plug the cable back in, then release BOOT. This forces the board into download mode.
4. On Linux, if the port is listed but cannot be opened, add your user to the `dialout` group and log in again.

### Option B: command line (esptool)

1. Install [Python 3](https://www.python.org/downloads/), then run:
   ```sh
   python -m pip install esptool
   ```
2. Download `meu-dot-waveshare-0.5.4-full.bin` and `SHA256SUMS.txt` from the [latest release](https://github.com/Hellridergo/meu-dot/releases/latest).
3. Optional: check the download against `SHA256SUMS.txt`. On macOS or Linux run `shasum -a 256 meu-dot-waveshare-0.5.4-full.bin`. On Windows run `certutil -hashfile meu-dot-waveshare-0.5.4-full.bin SHA256`.
4. Find the board's port. On macOS it is `/dev/cu.usbmodem…`. On Linux it is `/dev/ttyACM0`. On Windows it is `COM3`, `COM4` and so on, listed under *Ports* in Device Manager.
5. Write the full image, replacing `PORT` with your port:
   ```sh
   python -m esptool --chip esp32s3 --port PORT write_flash 0x0 meu-dot-waveshare-0.5.4-full.bin
   ```

> [!NOTE]
> The **full** image erases the board's pairing and settings, which is what you want the first time. For later updates, use the **app** image at `0x10000`, which keeps them (see [Updating](#11-updating)).

## 3. Install the app on your phone

1. On the phone, open the [latest release](https://github.com/Hellridergo/meu-dot/releases/latest) and download `meu-dot-android-0.5.4.apk`.
2. Open the downloaded file. Android asks for permission to install apps from your browser or file manager. Allow it for that app only.
3. **Samsung phones:** if the install button is greyed out, turn off *Auto Blocker* in *Settings → Security and privacy*. You can turn it back on afterwards.
4. If Google Play Protect warns about an unknown app, choose to install anyway. The APK is built from this repository and signed with the project's own key.
5. Open **Meu Dot** and allow *Dispositivos próximos* (Nearby devices) and notifications.

> [!IMPORTANT]
> "App not installed" usually means the phone already has a **newer** version, or a version **signed with a different key**. The installed version is shown at the bottom of the app's *Ajustes* tab. See also [Troubleshooting](troubleshooting.md#app-not-installed).

## 4. Deploy your bridge

The bridge is a small website that sits between your phone and your Dot. It runs on **ChatGPT Sites**, and only your ChatGPT account can open its panel.

Follow the [bridge guide](bridge.md). Once you are done you will have three things:

- the bridge address, for example `https://your-bridge.chatgpt.site`;
- a **phone token**, copied from the bridge panel;
- the bridge's **MCP plugin**, ready to add to a conversation.

## 5. Connect your Dot to the bridge

1. In ChatGPT, open the conversation with **your Dot** that the board should talk to.
2. Enable the bridge's plugin in that conversation.
3. Open the bridge panel at `https://your-bridge.chatgpt.site/painel`, copy the instruction it shows and send it in the Dot conversation. In short, the instruction asks the Dot to:
   - subscribe to `device.transcript` and `device.reply`;
   - send the short answer to the board first with `send_message`;
   - use `ask_confirmation` when it needs a decision from you.
4. Back in the panel, both subscriptions should show *Assinatura confirmada* (subscription confirmed).

## 6. Connect the app to the bridge

1. In the app, open **Ajustes → Conexão com seu Dot** (Settings → Connection with your Dot).
2. In *Endereço da ponte* (bridge address), type your bridge address.
3. In *Token do celular* (phone token), paste the token from the panel. The panel hides it again after 30 seconds.
4. Tap *Salvar conexão* (save connection). The token is stored encrypted with the Android Keystore.

## 7. Choose how to hear and be heard

Open **Ajustes → Voz e provedores** (Voice and providers). Speech recognition and reply voice are independent choices:

| Recognition (*Reconhecimento da sua fala*) | What leaves the phone |
| --- | --- |
| *Whisper · no celular*. First tap *Baixar modelo · 60 MB* once. | Only the text goes to your bridge. |
| *Google · Gemini 3.5 Transcribe*. Needs a Gemini API key. | The recording goes to Google. |

| Reply voice (*Leitura da resposta*) | What leaves the phone |
| --- | --- |
| *Android · síntese local*. Needs a Portuguese offline voice installed. | Nothing. |
| *Google · Gemini 3.8 Flash-Lite TTS*. Needs a Gemini key; pick one of 30 voices. | The reply text goes to Google. |
| *Fish Audio · Realtime WebSocket*. Needs a Fish key and a 32-character voice ID. | The reply text goes to Fish Audio. |

If the cloud voice fails or sends no audio for 8 s, the app reads the reply with the Android voice instead. Providers charge your own account.

## 8. Pair the board with the phone

1. Make sure the phone's Bluetooth is on and the board is on (hold **PWR** for half a second to turn it on).
2. In the app, on **Início** (Home), tap **Conectar Waveshare** (connect Waveshare).
3. The board shows a 6-digit *Código Bluetooth* (Bluetooth code). Check that the phone shows the same number, then confirm.
4. The app now shows *Waveshare conectada* (connected) with the battery level, and the board shows the Dot with *Segure para falar* (hold to talk).

## 9. Your first conversation

1. Hold the Dot on the screen, or hold **BOOT**. The ring that appears around the screen counts up to 30 s, and the board shows *Ouvindo* (listening).
2. Say: **"Responda apenas jabuticaba"** ("Reply only with jabuticaba").
3. Let go. The board shows *Enviando* (sending), then *Pensando* (thinking) while your Dot works.
4. The answer appears as *Dot respondeu* (Dot answered), and the board reads it aloud. Tap *Ler resposta* (read the answer) to read long answers page by page.
5. When the Dot asks something, choose *Confirmar* (confirm) or *Recusar* (decline).

To see how long each step took, open **Ajustes → Diagnóstico de resposta** in the app.

## 10. Everyday use

| On the board | Does |
| --- | --- |
| Hold the Dot, or hold BOOT | Records. Release to send; swipe up while recording to discard. |
| Swipe down from the top, or tap the sliders icon | Opens the settings: volume, brightness, Bluetooth and button shortcuts. |
| Locked screen (*Bloqueado*) | Swipe up from the lower half to unlock. |
| Short press on PWR | Turns the screen off and locks it. |
| Hold PWR for 3 s | Powers off (*Até logo*, see you). Hold PWR for half a second to power on. |

The BOOT button can be reassigned under *Atalhos dos botões* (button shortcuts) to: talk, open settings, mute, repeat the last reply, or nothing.

## 11. Updating

- **Board.** Use the web installer again and choose *Update*, or write the **app** image, which keeps pairing and settings:
  ```sh
  python -m esptool --chip esp32s3 --port PORT write_flash 0x10000 meu-dot-waveshare-0.5.4-app.bin
  ```
- **Phone.** Install the new APK over the old one. Your connection and keys are kept as long as the signature is the same.

Something not working? See [Troubleshooting](troubleshooting.md).
