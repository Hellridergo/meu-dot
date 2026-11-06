# Primeiros passos

Este guia leva você da placa na caixa até a primeira resposta falada. Não é preciso experiência com ESP32 nem com desenvolvimento Android. Reserve cerca de uma hora, quase toda para configurar a ponte.

**Conteúdo**

1. [Separe o hardware](#1-separe-o-hardware)
2. [Grave o firmware na placa](#2-grave-o-firmware-na-placa)
3. [Instale o app no celular](#3-instale-o-app-no-celular)
4. [Publique sua ponte](#4-publique-sua-ponte)
5. [Conecte o seu Dot à ponte](#5-conecte-o-seu-dot-à-ponte)
6. [Conecte o app à ponte](#6-conecte-o-app-à-ponte)
7. [Escolha como ouvir e ser ouvido](#7-escolha-como-ouvir-e-ser-ouvido)
8. [Pareie a placa com o celular](#8-pareie-a-placa-com-o-celular)
9. [Sua primeira conversa](#9-sua-primeira-conversa)
10. [Uso no dia a dia](#10-uso-no-dia-a-dia)
11. [Atualizar](#11-atualizar)

## 1. Separe o hardware

| Você precisa | Para quê |
| --- | --- |
| **Waveshare ESP32-S3-Touch-AMOLED-1.75C** | É o aparelho. Vem com bateria (SKU 33691) ou sem (SKU 33692), num case de alumínio com espaço para a bateria. |
| Bateria Li-ion de 3,7 V com plugue **MX1.25 de 2 pinos** | Só se você comprou a placa sem bateria. Sem bateria, a placa funciona ligada ao USB. |
| Cabo USB-C **de dados** | Alguns cabos só carregam. Se o computador nunca enxergar a placa, troque o cabo. |
| Celular Android | Android 12 ou superior, ARM64 (quase todos os celulares desde 2019), com Bluetooth. |
| Computador | Windows, macOS ou Linux, para gravar o firmware uma vez. |
| Conta do ChatGPT | Com acesso a **conversas com Dot**, **plugins MCP personalizados** e **ChatGPT Sites**. |

> [!WARNING]
> Se for ligar a bateria você mesmo, confira a polaridade marcada ao lado do conector antes de encaixar. Uma bateria invertida pode queimar a placa.

## 2. Grave o firmware na placa

Escolha um dos dois caminhos. O instalador pelo navegador não exige instalar nada no computador.

### Opção A: instalador pelo navegador (recomendado)

1. Abra **https://hellridergo.github.io/meu-dot/flasher/** no **Chrome** ou no **Edge**, num computador. O Safari e o Firefox não têm Web Serial.
2. Ligue a placa ao computador com o cabo USB-C.
3. Clique em **Instalar o Meu Dot** e escolha a porta que aparecer. Normalmente ela se chama *USB JTAG/serial debug unit*.
4. Confirme a instalação e espere cerca de um minuto, sem desligar o cabo.
5. Ao terminar, a placa reinicia e mostra o Dot âmbar.

Se nenhuma porta aparecer:

1. Desligue a placa do cabo.
2. Segure o botão **BOOT**.
3. Ligue o cabo de novo e só então solte o BOOT. Isso coloca a placa no modo de gravação.
4. No Linux, se a porta aparecer mas não abrir, adicione seu usuário ao grupo `dialout` e entre de novo na sessão.

### Opção B: linha de comando (esptool)

1. Instale o [Python 3](https://www.python.org/downloads/) e rode:
   ```sh
   python -m pip install esptool
   ```
2. Baixe o `meu-dot-waveshare-0.5.4-full.bin` e o `SHA256SUMS.txt` da [última versão](https://github.com/Hellridergo/meu-dot/releases/latest).
3. Opcional: confira o arquivo com o `SHA256SUMS.txt`.
   - No macOS ou Linux: `shasum -a 256 meu-dot-waveshare-0.5.4-full.bin`.
   - No Windows: `certutil -hashfile meu-dot-waveshare-0.5.4-full.bin SHA256`.
4. Descubra a porta da placa.
   - macOS: `/dev/cu.usbmodem…`.
   - Linux: `/dev/ttyACM0`.
   - Windows: `COM3`, `COM4` etc., em *Portas* no Gerenciador de Dispositivos.
5. Grave a imagem completa, trocando `PORTA` pela sua porta:
   ```sh
   python -m esptool --chip esp32s3 --port PORTA write_flash 0x0 meu-dot-waveshare-0.5.4-full.bin
   ```

> [!NOTE]
> A imagem **completa** apaga o pareamento e os ajustes da placa, o que é o esperado na primeira vez. Nas atualizações, use a imagem **do app** em `0x10000`, que preserva os dois (veja [Atualizar](#11-atualizar)).

## 3. Instale o app no celular

1. No celular, abra a [última versão](https://github.com/Hellridergo/meu-dot/releases/latest) e baixe o `meu-dot-android-0.5.4.apk`.
2. Abra o arquivo baixado. O Android pede permissão para instalar apps a partir do navegador ou do gerenciador de arquivos. Libere só para esse app.
3. **Celulares Samsung:** se o botão de instalar estiver apagado, desative o *Bloqueador automático* em *Configurações → Segurança e privacidade*. Depois você pode reativá-lo.
4. Se o Google Play Protect avisar que o app é desconhecido, escolha instalar mesmo assim. O APK é gerado a partir deste repositório e assinado com a chave do próprio projeto.
5. Abra o **Meu Dot** e permita *Dispositivos próximos* e as notificações.

> [!IMPORTANT]
> "App não instalado" quase sempre significa que o celular já tem uma versão **mais nova**, ou uma versão **assinada com outra chave**. A versão instalada aparece no fim da aba *Ajustes* do app. Veja também [Solução de problemas](solucao-de-problemas.md#app-não-instalado).

## 4. Publique sua ponte

A ponte é um pequeno site que fica entre o celular e o seu Dot. Ela roda no **ChatGPT Sites**, e só a sua conta do ChatGPT abre o painel dela.

Siga o [guia da ponte](ponte.md). No fim, você terá três coisas:

- o endereço da ponte, por exemplo `https://sua-ponte.chatgpt.site`;
- o **token do celular**, copiado do painel da ponte;
- o **plugin MCP** da ponte, pronto para entrar numa conversa.

## 5. Conecte o seu Dot à ponte

1. No ChatGPT, abra a conversa com **o seu Dot** que vai atender a placa.
2. Ative o plugin da ponte nessa conversa.
3. Abra o painel da ponte em `https://sua-ponte.chatgpt.site/painel`, copie a instrução que aparece e envie na conversa do Dot. Em resumo, a instrução pede que o Dot:
   - assine `device.transcript` e `device.reply`;
   - mande primeiro a resposta curta à placa com `send_message`;
   - use `ask_confirmation` quando precisar da sua decisão.
4. De volta ao painel, as duas assinaturas devem aparecer como *Assinatura confirmada*.

## 6. Conecte o app à ponte

1. No app, abra **Ajustes → Conexão com seu Dot**.
2. Em *Endereço da ponte*, digite o endereço da sua ponte.
3. Em *Token do celular*, cole o token do painel. O painel volta a esconder o token depois de 30 segundos.
4. Toque em *Salvar conexão*. O token fica cifrado pelo Android Keystore.

## 7. Escolha como ouvir e ser ouvido

Abra **Ajustes → Voz e provedores**. O reconhecimento da fala e a voz da resposta são escolhas independentes:

| Reconhecimento da sua fala | O que sai do celular |
| --- | --- |
| *Whisper · no celular*. Toque antes uma vez em *Baixar modelo · 60 MB*. | Só o texto, para a sua ponte. |
| *Google · Gemini 3.5 Transcribe*. Precisa de uma chave da API Gemini. | A gravação, para o Google. |

| Leitura da resposta | O que sai do celular |
| --- | --- |
| *Android · síntese local*. Precisa de uma voz offline em português instalada. | Nada. |
| *Google · Gemini 3.8 Flash-Lite TTS*. Precisa de uma chave Gemini; escolha uma de 30 vozes. | O texto da resposta, para o Google. |
| *Fish Audio · Realtime WebSocket*. Precisa da chave Fish e do ID da voz, com 32 caracteres. | O texto da resposta, para a Fish Audio. |

Se a voz em nuvem falhar ou passar 8 s sem mandar áudio, o app lê a resposta com a voz do Android. Cada provedor cobra na sua própria conta.

## 8. Pareie a placa com o celular

1. Confira se o Bluetooth do celular está ligado e se a placa está ligada. Para ligar a placa, segure o **PWR** por meio segundo.
2. No app, na aba **Início**, toque em **Conectar Waveshare**.
3. A placa mostra um *Código Bluetooth* de 6 dígitos. Confira se o celular mostra o mesmo número e confirme.
4. O app passa a mostrar *Waveshare conectada*, com a bateria, e a placa mostra o Dot com *Segure para falar*.

## 9. Sua primeira conversa

1. Segure o Dot na tela, ou segure o **BOOT**. Um anel em volta da tela conta até 30 s, e a placa mostra *Ouvindo*.
2. Diga: **"Responda apenas jabuticaba"**.
3. Solte. A placa mostra *Enviando* e depois *Pensando*, enquanto o seu Dot trabalha.
4. A resposta aparece em *Dot respondeu*, e a placa lê em voz alta. Toque em *Ler resposta* para ler respostas longas página por página.
5. Quando o Dot perguntar algo, escolha *Confirmar* ou *Recusar*.

Para ver quanto tempo levou cada etapa, abra **Ajustes → Diagnóstico de resposta** no app.

## 10. Uso no dia a dia

| Na placa | Faz |
| --- | --- |
| Segurar o Dot, ou segurar o BOOT | Grava. Solte para enviar; deslize para cima durante a gravação para descartar. |
| Deslizar de cima para baixo, ou tocar no ícone de ajustes | Abre volume, brilho, Bluetooth e atalhos dos botões. |
| Tela *Bloqueado* | Deslize para cima a partir da metade de baixo para desbloquear. |
| Toque curto no PWR | Apaga e bloqueia a tela. |
| Segurar o PWR por 3 s | Desliga a placa (*Até logo*). Para ligar, segure o PWR por meio segundo. |

Em *Atalhos dos botões*, o BOOT pode virar: falar, abrir ajustes, silenciar, repetir a última resposta ou nenhum atalho.

## 11. Atualizar

- **Placa.** Use de novo o instalador pelo navegador e escolha atualizar, ou grave a imagem **do app**, que preserva pareamento e ajustes:
  ```sh
  python -m esptool --chip esp32s3 --port PORTA write_flash 0x10000 meu-dot-waveshare-0.5.4-app.bin
  ```
- **Celular.** Instale o APK novo por cima do antigo. A conexão e as chaves continuam, desde que a assinatura seja a mesma.

Algo não funcionou? Veja [Solução de problemas](solucao-de-problemas.md).
