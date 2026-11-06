# Security · Segurança

**English.** Please report vulnerabilities privately through GitHub's *Report a vulnerability* button (Security → Advisories), not in public issues. Never paste your bridge token, `MOBILE_TOKEN`, `EVENT_KEY_HEX`, API keys or `debug.p12` into an issue or pull request.

How secrets are handled:

- **Phone.** API keys and the bridge token are encrypted with an AES-GCM key from the Android Keystore. Each key is sent only to its provider's fixed HTTPS endpoint. Screens that show keys use `FLAG_SECURE`.
- **Board ↔ phone.** Bluetooth LE Secure Connections with a passkey shown on the board. Every write needs an authenticated, encrypted link.
- **Bridge.** Phone requests need `Authorization: Bearer <MOBILE_TOKEN>`, compared in constant time; browser requests with an `Origin` header are refused. The panel, MCP tool calls and event subscriptions need the owner's ChatGPT identity (`DOT_OWNER_EMAIL`). Webhook secrets are stored encrypted with `EVENT_KEY_HEX`, and events go only to OpenAI's receiver.

**Português.** Relate vulnerabilidades de forma privada pelo botão *Report a vulnerability* do GitHub (Security → Advisories), e não em issues públicas. Nunca cole em issues ou pull requests o token da ponte, o `MOBILE_TOKEN`, o `EVENT_KEY_HEX`, chaves de API ou o `debug.p12`.

Como os segredos são tratados:

- **Celular.** As chaves de API e o token da ponte são cifrados com uma chave AES-GCM do Android Keystore. Cada chave só vai para o endpoint HTTPS fixo do seu provedor. As telas que mostram chaves usam `FLAG_SECURE`.
- **Placa ↔ celular.** Bluetooth LE Secure Connections com código mostrado na placa. Toda escrita exige um link autenticado e cifrado.
- **Ponte.** As requisições do celular exigem `Authorization: Bearer <MOBILE_TOKEN>`, comparado em tempo constante; requisições de navegador, com `Origin`, são recusadas. O painel, as ferramentas MCP e as assinaturas de eventos exigem a identidade do dono no ChatGPT (`DOT_OWNER_EMAIL`). Os segredos de webhook ficam cifrados com o `EVENT_KEY_HEX`, e os eventos só vão para o receptor da OpenAI.
