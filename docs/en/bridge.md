# The bridge

The bridge is the only piece that runs in the cloud. It has four jobs:

- receive the text of what you said from the phone;
- deliver it to your Dot as an MCP event;
- hold the Dot's answer until the phone picks it up;
- record your choice when the Dot asks for a decision.

It is a small [Next.js](https://nextjs.org) app built for **ChatGPT Sites**, and it uses three Sites resources:

| Resource | Used for |
| --- | --- |
| **D1** database (binding `DB`) | Transcripts, reply cards, decisions, event subscriptions and the delivery queue. |
| **R2** bucket (binding `BUCKET`) | Optional. Holds the APK that the owner can download from the panel. |
| **MCP capability** | Exposes `/mcp`, which ChatGPT adds to a conversation as a plugin. |

> [!NOTE]
> ChatGPT Sites, MCP plugins and Dot conversations are OpenAI features that change over time and may not be available on every plan. The menu names below may differ from what you see.

## 1. Create the site

1. In ChatGPT, create a new site in **Sites**.
2. Publish the contents of this repository's [`bridge/`](../../bridge) folder to it. Sites generates its own build scaffolding (`scripts/`, `vite.config.ts`, `package.json`). The copies in this folder match the version that is deployed.
3. Enable a **D1** database named `DB`, an **R2** bucket named `BUCKET` and the **MCP** capability. [`bridge/.openai/hosting.example.json`](../../bridge/.openai/hosting.example.json) shows the expected shape; Sites fills in your own project ID.
4. Apply the database migrations in [`bridge/drizzle/`](../../bridge/drizzle), in order.

## 2. Set the secrets

Set these four values as secrets in the site's settings. Never commit them.

| Secret | What it is | How to make one |
| --- | --- | --- |
| `DOT_OWNER_EMAIL` | The e-mail of **your** ChatGPT account. Only this account can open the panel, use the MCP tools and subscribe to events. | Your account e-mail. |
| `MOBILE_TOKEN` | The token the phone sends on every request. At least 32 characters. | `openssl rand -hex 32` |
| `EVENT_KEY_HEX` | The key that encrypts the webhook secrets of your event subscriptions. Exactly 64 hex characters. | `openssl rand -hex 32` |
| `RELEASE_TOKEN` | Only for uploading an APK to `/_release/android` so the panel can offer it. At least 32 characters. | `openssl rand -hex 32` |

## 3. Open the panel

Go to `https://your-bridge.chatgpt.site/painel` and sign in with the owner account. The panel shows:

- **Bridge address and phone token.** You paste them into the app under *Ajustes → Conexão com seu Dot*. Tap to reveal the token; it hides itself again after 30 s.
- **The instruction for your Dot.** Paste it into the Dot conversation after enabling the plugin.
- **Diagnostics.** Whether the phone checked in recently and whether both event subscriptions are confirmed.

## 4. Connect your Dot

1. Open the conversation with your Dot and enable the bridge plugin.
2. Send the instruction from the panel. With it, the Dot subscribes to the two events below and knows to answer on the board first.
3. Back in the panel, both subscriptions should show as confirmed. Enabling the plugin alone does not subscribe; the Dot has to ask for the subscription in the conversation.

## Reference

### HTTP API (used by the phone)

Every phone request carries `Authorization: Bearer <MOBILE_TOKEN>`. Browser requests with an `Origin` header are refused.

| Route | Method | Purpose |
| --- | --- | --- |
| `/state` | GET | Current card (reply or decision), readiness and the bridge's input mode. |
| `/transcript/{uuid}` | POST | Text of a recording: `{text, audio_seconds}`. Repeating the same ID is safe. |
| `/choice` | POST | The answer to a decision: `{id, option}` with `yes`, `no` or `dismiss`. |
| `/receipt` | POST | Tells the bridge that a notice was shown and played, so the next card can come forward. |
| `/health` | GET | Service name, version and accepted clients. |

### MCP (used by your Dot)

| Tool | Does |
| --- | --- |
| `send_message` | Shows a short reply on the board (up to 600 characters). |
| `ask_confirmation` | Shows a question with *Confirmar* and *Recusar*. |
| `get_request` | Reads a card and the option you picked. |
| `get_voice_input` | Recovers the text of a recording by `recording_id`. |
| `get_device_status` | Shows whether the phone is online and whether the subscriptions are active. |

| Event | Sent when |
| --- | --- |
| `device.transcript` | A new recording has been transcribed. |
| `device.reply` | You confirmed, declined or dismissed a card on the board. |

Events are delivered as signed webhooks (Standard Webhooks) only to OpenAI's event receiver, carry idempotent IDs, and are retried from a persistent queue.

## Testing the bridge locally

With Node.js 22.13 or newer, from the `bridge/` folder:

```sh
node --test tests/bridge.test.mjs
```

The tests use a local SQLite database, a test account and a simulated event receiver. They cover authentication, signatures, retries, the receipt flow, queue order, explicit confirmation and correlation.
