# Changelog · Histórico de versões

## 0.5.4 — 2026-11-05

**English**
- **Board.** When the reply audio arrives slower than 1.25× real time, playback starts with 1 s buffered. After a pause, it waits for 1.5 s of audio, so a slow Bluetooth link causes at most one longer pause instead of a series of cuts.
- **App (first public build).** The bridge address starts empty and *Abrir painel da ponte* opens the panel of the bridge you configured. Any `*.chatgpt.site` bridge uses the text mode. The greeting is generic and the app no longer shows third-party logos.

**Português**
- **Placa.** Quando a voz da resposta chega mais devagar que 1,25 vez o tempo real, a reprodução começa com 1 s guardado. Depois de uma pausa, espera 1,5 s de áudio. Assim, um Bluetooth lento causa no máximo uma pausa maior, e não uma série de cortes.
- **App (primeira versão pública).** O endereço da ponte começa vazio, e *Abrir painel da ponte* abre o painel da ponte que você configurou. Qualquer ponte `*.chatgpt.site` usa o modo de texto. A saudação é genérica, e o app não mostra mais logotipos de terceiros.

## 0.5.3

- **EN.** Lock screen: dims at 15 s, locks at 30 s, swipe up to unlock. Short PWR press turns the screen off; holding PWR for 3 s powers off through the AXP2101. The phone starts streaming a reply only with 1 s of speech ready, because cloud voices send the first words early.
- **PT.** Tela de bloqueio: escurece aos 15 s, bloqueia aos 30 s e desbloqueia deslizando para cima. Um toque curto no PWR apaga a tela, e segurar o PWR por 3 s desliga pelo AXP2101. O celular só começa a enviar a resposta quando tem 1 s de fala pronta, porque as vozes em nuvem mandam as primeiras palavras antes do resto.

## 0.5.2

- **EN.** If the cloud voice fails or stays silent for 8 s, the reply is read with the offline Android voice. The board reports every reply's playback (opcode `34`), so it can be measured without a USB cable.
- **PT.** Se a voz em nuvem falhar ou ficar 8 s em silêncio, a resposta é lida com a voz offline do Android. A placa informa como tocou cada resposta (código `34`), o que permite medir sem cabo USB.

## 0.5.1

- **EN.** Fixed choppy replies. The audio task now runs above the display task, because the I2S DMA holds only about 90 ms.
- **PT.** Corrigiu a voz que picotava. A tarefa de áudio agora roda acima da tarefa da tela, porque o DMA do I2S guarda só uns 90 ms.

## 0.5.0 — "Lume"

- **EN.** Visual redesign on the board and in the app: an amber Dot (`#F99D22`), true black with a warm halo, and partial repaint that sends only what changed to the panel.
- **PT.** Redesenho visual na placa e no app: Dot âmbar (`#F99D22`), preto verdadeiro com halo quente, e repintura parcial que envia ao painel só o que mudou.

## 0.4.2 – 0.4.4 — Fast Bluetooth · Bluetooth rápido

- **EN.**
  - The microphone streams while you speak, about 15 ms after release, instead of several seconds.
  - Notifications and writes without response replace one-packet-at-a-time transfers.
  - The radio switches to the 2M PHY.
  - The microphone opens at touch-down, so the first syllable is kept.
  - Reply audio is sent in pieces, paced to 2.5× real time, and survives a busy link.
  - Capabilities are announced again after a bonded reconnect.
- **PT.**
  - O microfone envia enquanto você fala, cerca de 15 ms depois de soltar, em vez de vários segundos.
  - Notificações e escrita sem confirmação substituem o envio de um pacote por vez.
  - O rádio passa a usar o PHY 2M.
  - O microfone abre no toque, e a primeira sílaba não se perde.
  - A voz da resposta vai em pedaços, no ritmo de 2,5 vezes o tempo real, e resiste a um link ocupado.
  - Os recursos são anunciados de novo quando um celular pareado reconecta.

## 0.4.0 – 0.4.1 — Meu Dot

- **EN.** New app with Home, Conversations, Device and Settings. Battery indicator on the board. Playback receipts, and a timing log for every request.
- **PT.** App novo com Início, Conversas, Dispositivo e Ajustes. Indicador de bateria na placa. Recibos de reprodução e registro dos tempos de cada pedido.

## 0.3 — Dot de bolso

- **EN.** First complete version: board, Android app with Whisper, Gemini and Fish Audio, and the ChatGPT Sites bridge.
- **PT.** Primeira versão completa: placa, app Android com Whisper, Gemini e Fish Audio, e a ponte no ChatGPT Sites.
