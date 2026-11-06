# Contributing · Como contribuir

**English.** Issues and pull requests are welcome.

1. For a bug, open an issue with the board firmware version, the app version (bottom of *Ajustes*) and the relevant `dot_ble`, `dot_audio` or `MeuDotBle` log lines.
2. Before a pull request, run `sh tests/run.sh`, and add or update a test when you change `voice.c`, `dot_ui.c` or the Bluetooth protocol.
3. Keep the protocol backwards compatible: new features are announced with a capability bit and old peers ignore unknown opcodes. Document new opcodes in `docs/en/how-it-works.md` and `docs/pt-BR/como-funciona.md`.
4. The device and app speak Portuguese. Keep user-facing strings in pt-BR and code comments in English.

**Português.** Issues e pull requests são bem-vindos.

1. Para um bug, abra uma issue com a versão do firmware, a versão do app (no fim de *Ajustes*) e as linhas de registro `dot_ble`, `dot_audio` ou `MeuDotBle` relevantes.
2. Antes de um pull request, rode `sh tests/run.sh`, e acrescente ou atualize um teste ao mudar o `voice.c`, o `dot_ui.c` ou o protocolo Bluetooth.
3. Mantenha o protocolo compatível com versões anteriores: recursos novos são anunciados com um bit de capacidade, e quem não os conhece ignora os códigos novos. Documente os códigos novos em `docs/en/how-it-works.md` e `docs/pt-BR/como-funciona.md`.
4. A placa e o app falam português. Mantenha os textos da interface em pt-BR e os comentários do código em inglês.
