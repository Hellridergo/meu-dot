# Solução de problemas

Comece pelo sintoma que você está vendo. A maioria dos problemas está em um de três elos: placa ↔ celular (Bluetooth), celular ↔ ponte (internet) ou ponte ↔ Dot (assinatura).

## Instalação

### O computador não enxerga a placa

- Troque o cabo USB-C; muitos cabos só carregam.
- Force o modo de gravação:
  1. Desligue a placa do cabo.
  2. Segure o **BOOT**, ligue o cabo de novo e só então solte o BOOT.
- **Windows:** a porta aparece em *Portas (COM e LPT)*. O USB nativo do ESP32-S3 não precisa de driver.
- **Linux:** adicione seu usuário ao grupo `dialout` (`sudo usermod -aG dialout $USER`) e entre de novo na sessão.

### App não instalado

O Android mostra só *"App não instalado"*, sem dizer o motivo, quando:

- **o celular já tem uma versão mais nova.** O Android nunca instala uma versão mais antiga por cima de uma mais nova. Confira se baixou o arquivo certo: um APK antigo da `0.1` tem só 33 KB;
- **o app instalado foi assinado com outra chave**, por exemplo um que você compilou com outro `debug.p12`. Desinstale antes. Isso apaga o pareamento e as chaves do app;
- **o *Bloqueador automático* da Samsung está ligado.** Desative em *Configurações → Segurança e privacidade*.

Com um computador e a depuração USB, `adb install -r meu-dot-android-0.5.4.apk` mostra o motivo exato, por exemplo `INSTALL_FAILED_VERSION_DOWNGRADE`.

## Pareamento e conexão

| Sintoma | Confira |
| --- | --- |
| A placa fica na tela de pareamento | O Bluetooth do celular está ligado. *Dispositivos próximos* está liberado para o Meu Dot. O celular está perto. Toque de novo em **Conectar Waveshare**. |
| O código da placa é diferente do celular | Cancele no celular e tente de novo. Se a placa foi regravada com a imagem completa, apague antes o pareamento antigo nas configurações de Bluetooth do celular. |
| *Dot offline* na placa | O celular está sem internet, ou o endereço ou o token da ponte estão errados. Abra *Ajustes → Conexão com seu Dot* e salve de novo. |
| Conectado, mas o Dot nunca responde | No painel da ponte, as duas assinaturas precisam aparecer como confirmadas. Se não aparecerem, envie de novo a instrução do painel na conversa do Dot e confira se o plugin está ativo ali. |
| *Token recusado* | Copie de novo o token do painel. Ele precisa ser idêntico ao `MOBILE_TOKEN`. |

## Áudio

| Sintoma | Confira |
| --- | --- |
| Sem voz, mas o texto aparece | *Ouvir respostas na Waveshare* está ligado no app. A placa não está no mudo e o volume está acima de 0. Para a voz do Android, há uma voz **offline** em português instalada. |
| A voz pausa no meio | Outros aparelhos Bluetooth no celular, como óculos inteligentes ou fones, dividem o rádio. A placa pausa uma vez para juntar 1,5 s, em vez de cortar várias vezes. Aproxime o celular ou desconecte o outro aparelho. |
| A voz em nuvem falha | Confira a chave e os créditos do provedor. Depois de um erro ou de 8 s sem áudio, o app passa para a voz do Android. Um erro *400* do Gemini costuma indicar que a voz ou o modelo escolhido não está disponível na sua chave. |
| *O trecho foi curto* | Segure por pelo menos um quarto de segundo enquanto fala. |

## Como ler os registros

- **Placa** (com o USB conectado):
  ```sh
  idf.py -p PORTA monitor
  ```
  - `dot_ble` mostra o motivo de cada desconexão. `0x208` é o rádio sem resposta, `0x213` é o celular encerrando e `0x216` é a placa encerrando.
  - `dot_audio` mostra as pausas e as falhas do alto-falante em cada resposta.
  - `dot_tempo` mostra o tempo de soltar até o primeiro som.
- **Celular** (com a depuração USB):
  ```sh
  adb logcat -s MeuDotBle MeuDotTiming MeuDotVoz
  ```
  - O relatório da placa sobre cada resposta aparece como `MeuDotBle: placa tocou …`, então dá para medir sem cabo na placa.
  - O detalhamento dos tempos medido pelo app está em *Ajustes → Diagnóstico de resposta*.

## Compilação

- **"Unknown CMake command" ou falhas do compilador sem mensagem no macOS.** Veja se o *Otimizar armazenamento* do iCloud tirou do disco arquivos do ESP-IDF. Rode `ls -lO` num arquivo do ESP-IDF e procure `dataless`. Mantenha a pasta do ESP-IDF baixada (Finder → botão direito → *Manter Baixado*), ou instale o ESP-IDF fora das pastas sincronizadas.
- **O APK não instala por cima.** Compile de novo com o mesmo `debug.p12` usado antes.
