# Compilar a partir do código

Tudo o que está nas releases pode ser recompilado a partir deste repositório. Esta página é para desenvolvedores. Para apenas usar o Meu Dot, siga os [Primeiros passos](primeiros-passos.md).

| Parte | Ferramentas |
| --- | --- |
| Firmware | [ESP-IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/). O BSP Waveshare 3.0.0 e o LVGL 9.5.0 vêm do gerenciador de componentes, fixados no `dependencies.lock`. |
| App Android | JDK 17, Android SDK Platform 35, Build Tools 36.1.0, NDK r28c, CMake e Ninja. Não usa Gradle. |
| Ponte | Node.js 22.13+ e um projeto no ChatGPT Sites. |
| Simulador e arte | Clang com alvo `wasm32` e `wasm-ld`; Python 3 com Pillow. |
| Testes no computador | Um compilador C, o JDK 17 e o Node.js. |

## Firmware

```sh
. $IDF_PATH/export.sh            # ou o terminal do ESP-IDF no Windows
cd firmware
idf.py build
idf.py -p PORTA flash monitor     # grava bootloader, tabela de partições e app
```

- O `sdkconfig` está no repositório, então o build reproduz a configuração das releases. O `sdkconfig.defaults` guarda só o essencial.
- A tabela de partições ([`partitions.csv`](../../firmware/partitions.csv)) tem a NVS (pareamento e ajustes) em `0x9000` e um único app de 6 MB em `0x10000`. Gravar só a imagem do app preserva o pareamento.
- Para gerar uma imagem completa para `0x0`:
  ```sh
  cd build
  python -m esptool --chip esp32s3 merge_bin -o meu-dot-full.bin \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 dot_de_bolso.bin
  ```
- A porta USB-C da placa é o USB Serial/JTAG nativo do ESP32-S3. Não precisa de driver, e o console roda a 115200 baud.

### Onde fica cada coisa

| Arquivo | Conteúdo |
| --- | --- |
| `firmware/main/main.c` | Início do display, canvas do LVGL, ciclo de 20 ms da interface e desligamento. |
| `firmware/main/voice.c` | Gravação, tarefa de envio do microfone, reprodução e reserva da resposta. |
| `firmware/main/link_ble.c` | Servidor GATT NimBLE, pareamento, negociação de recursos e fila de pacotes. |
| `firmware/main/battery.c` | Leitura do AXP2101, sem alterar nada. |
| `firmware/main/settings.c` | Botões, debounce e ajustes salvos na NVS. |
| `interface/dot_ui.c` | Todas as telas, gestos e animações, com repintura parcial. |
| `interface/art_data.inc` | Quadros do personagem e fontes, gerados pelo `tools/recursos.py`. |

## App Android

```sh
export ANDROID_HOME=/caminho/do/android-sdk
export JAVA_HOME=/caminho/do/jdk-17
export ANDROID_NDK_HOME=/caminho/do/android-ndk-r28c
bash android/build.sh /caminho/de/saida     # fora do repositório
```

O script:

1. baixa quatro JARs fixados (Java-WebSocket, MessagePack e SLF4J) e confere o SHA-256 de cada um;
2. compila a `libdot_voice.so` para arm64 a partir de uma revisão fixa do whisper.cpp. Use `WHISPER_SOURCE_DIR` para reaproveitar um checkout;
3. compila o Java com `javac`, converte com `d8`, alinha o APK a páginas de 16 KB e assina.

> [!IMPORTANT]
> No primeiro build, o `build.sh` cria uma chave de assinatura, `debug.p12`, na pasta de saída. **Guarde-a e mantenha-a privada.** O Android só instala uma atualização por cima de um app assinado com a mesma chave. A chave nunca entra neste repositório. Defina `MEU_DOT_KEYSTORE_PASS` para protegê-la com uma senha sua; a padrão, `android`, só serve para uma chave descartável.

O app não usa framework de interface de terceiros:
- `Design.java` tem a paleta e os auxiliares de layout;
- `Companion.java`, o serviço;
- `Radio.java`, o protocolo Bluetooth;
- `VoiceOutput.java`, `CloudVoice.java` e `OfflineVoice.java`, os provedores de voz.

## Ponte

Veja [Ponte](ponte.md). Para rodar os testes dela:

```sh
cd bridge && node --test tests/bridge.test.mjs
```

## Simulador

O simulador compila o `interface/dot_ui.c` para WebAssembly e embute tudo num único HTML:

```sh
bash tools/previa.sh docs/simulator/index.html   # precisa do clang (wasm32) e do wasm-ld
```

## Arte do personagem

```sh
python tools/ambar.py      # originais azuis → art/quadros-ambar.png e art/personagem.png
python tools/recursos.py   # arte → interface/art_data.inc (RGB565 + máscara de 4 bits, fontes)
python tools/repo/banner.py
```

## Testes

```sh
sh tests/run.sh
```

| Conjunto | Confere |
| --- | --- |
| `tests/firmware/voice` | Compila o `voice.c` real no computador, com AddressSanitizer e UBSan. Codec, GATT, fila e agendador são simulados; o envio do microfone roda numa thread de verdade. São 18 cenários, incluindo streaming, cancelamento, limites de 30 s, falta de dados, pré-gravação, descarte e vínculo com a gravação. |
| `tests/firmware/ui` | `controls.c`: gestos, limite de 30 s, atalhos, tela de bloqueio e desligamento. `BatteryTest.c`: leitura do AXP2101. |
| `tests/android` | Contratos do Gemini e da Fish, reamostragem do PCM, formato BLE, a regra de começar com 1 s, correlação dos tempos e a segurança de avisos e decisões. |
| `bridge/tests` | Autenticação, assinaturas, novas tentativas, recibos, ordem da fila e correlação. |

## Depuração

- **Placa:** `idf.py -p PORTA monitor` mostra as tags `dot_ble`, `dot_audio`, `dot_tempo` e `dot_ui`.
- **Celular:**
  - `adb logcat -s MeuDotBle MeuDotTiming MeuDotVoz` mostra o link, o tempo de cada etapa e as trocas de voz.
  - O áudio da última resposta fica em `Android/data/br.bolso.dot/files/ultima-resposta.wav`.
