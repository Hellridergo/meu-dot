# A ponte

A ponte é a única peça que roda na nuvem. Ela tem quatro funções:

- receber do celular o texto do que você falou;
- entregar esse texto ao seu Dot como um evento MCP;
- guardar a resposta do Dot até o celular buscá-la;
- registrar a sua escolha quando o Dot pede uma decisão.

É um pequeno app [Next.js](https://nextjs.org) feito para o **ChatGPT Sites**, que usa três recursos do Sites:

| Recurso | Para quê |
| --- | --- |
| Banco **D1** (nome `DB`) | Transcrições, cartões de resposta, decisões, assinaturas de eventos e a fila de entregas. |
| Bucket **R2** (nome `BUCKET`) | Opcional. Guarda o APK que o dono pode baixar pelo painel. |
| Capacidade **MCP** | Expõe `/mcp`, que o ChatGPT adiciona a uma conversa como plugin. |

> [!NOTE]
> O ChatGPT Sites, os plugins MCP e as conversas com Dot são recursos da OpenAI que mudam com o tempo e podem não estar disponíveis em todos os planos. Os nomes dos menus abaixo podem ser diferentes dos que você vê.

## 1. Crie o site

1. No ChatGPT, crie um site novo no **Sites**.
2. Publique nele o conteúdo da pasta [`bridge/`](../../bridge) deste repositório. O Sites gera a própria estrutura de build (`scripts/`, `vite.config.ts`, `package.json`). As cópias desta pasta são as da versão publicada.
3. Ative um banco **D1** chamado `DB`, um bucket **R2** chamado `BUCKET` e a capacidade **MCP**. O arquivo [`bridge/.openai/hosting.example.json`](../../bridge/.openai/hosting.example.json) mostra o formato esperado; o Sites preenche o ID do seu projeto.
4. Aplique as migrações do banco, em ordem, que estão em [`bridge/drizzle/`](../../bridge/drizzle).

## 2. Configure os segredos

Configure estes quatro valores como segredos nas configurações do site. Nunca os coloque no repositório.

| Segredo | O que é | Como gerar |
| --- | --- | --- |
| `DOT_OWNER_EMAIL` | O e-mail da **sua** conta do ChatGPT. Só essa conta abre o painel, usa as ferramentas MCP e assina os eventos. | O e-mail da sua conta. |
| `MOBILE_TOKEN` | O token que o celular envia em toda requisição. Pelo menos 32 caracteres. | `openssl rand -hex 32` |
| `EVENT_KEY_HEX` | A chave que cifra os segredos de webhook das assinaturas. Exatamente 64 caracteres hexadecimais. | `openssl rand -hex 32` |
| `RELEASE_TOKEN` | Só para enviar um APK a `/_release/android` e o painel oferecê-lo. Pelo menos 32 caracteres. | `openssl rand -hex 32` |

## 3. Abra o painel

Abra `https://sua-ponte.chatgpt.site/painel` e entre com a conta dona. O painel mostra:

- **Endereço da ponte e token do celular.** Você cola os dois no app, em *Ajustes → Conexão com seu Dot*. Toque para revelar o token; ele volta a se esconder depois de 30 s.
- **A instrução para o seu Dot.** Cole na conversa do Dot depois de ativar o plugin.
- **Diagnóstico.** Mostra se o celular consultou a ponte há pouco e se as duas assinaturas de eventos estão confirmadas.

## 4. Conecte o seu Dot

1. Abra a conversa com o seu Dot e ative o plugin da ponte.
2. Envie a instrução do painel. Com ela, o Dot assina os dois eventos abaixo e sabe que deve responder primeiro na placa.
3. De volta ao painel, as duas assinaturas devem aparecer como confirmadas. Só ativar o plugin não cria a assinatura; o Dot precisa pedi-la na conversa.

## Referência

### API HTTP (usada pelo celular)

Toda requisição do celular leva `Authorization: Bearer <MOBILE_TOKEN>`. Requisições de navegador, com cabeçalho `Origin`, são recusadas.

| Rota | Método | Função |
| --- | --- | --- |
| `/state` | GET | Cartão atual (resposta ou decisão), prontidão e o modo de entrada da ponte. |
| `/transcript/{uuid}` | POST | Texto de uma gravação: `{text, audio_seconds}`. Repetir o mesmo ID é seguro. |
| `/choice` | POST | A resposta a uma decisão: `{id, option}` com `yes`, `no` ou `dismiss`. |
| `/receipt` | POST | Avisa que um aviso foi exibido e tocado, para o próximo cartão avançar. |
| `/health` | GET | Nome do serviço, versão e clientes aceitos. |

### MCP (usado pelo seu Dot)

| Ferramenta | Faz |
| --- | --- |
| `send_message` | Mostra uma resposta curta na placa (até 600 caracteres). |
| `ask_confirmation` | Mostra uma pergunta com *Confirmar* e *Recusar*. |
| `get_request` | Lê um cartão e a opção escolhida. |
| `get_voice_input` | Recupera o texto de uma gravação pelo `recording_id`. |
| `get_device_status` | Mostra se o celular está online e se as assinaturas estão ativas. |

| Evento | Quando é enviado |
| --- | --- |
| `device.transcript` | Uma gravação nova foi transcrita. |
| `device.reply` | Você confirmou, recusou ou dispensou um cartão na placa. |

Os eventos são entregues como webhooks assinados (Standard Webhooks), só ao receptor de eventos da OpenAI. Eles têm IDs idempotentes e são repetidos a partir de uma fila persistente.

## Testar a ponte no computador

Com o Node.js 22.13 ou superior, a partir da pasta `bridge/`:

```sh
node --test tests/bridge.test.mjs
```

Os testes usam um SQLite local, uma conta de teste e um receptor de eventos simulado. Eles cobrem autenticação, assinaturas, novas tentativas, recibos, ordem da fila, confirmação explícita e correlação.
