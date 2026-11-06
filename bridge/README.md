# Dot de bolso · Sites

Ponte pessoal entre um Android, a Waveshare ESP32-S3-Touch-AMOLED-1.75C e o Dot da conta proprietária. O cliente Android 0.2 transcreve no telefone. O Site armazena texto e decisões e envia eventos assinados ao chat do Dot que os assinar.

## Operação

- A página inicial é pública. O painel e a distribuição do APK exigem a identidade ChatGPT da pessoa proprietária.
- O painel fornece o endereço e permite copiar o token do celular. O Android apresenta esse token em HTTPS e o guarda no Keystore. A rota para o aplicativo anterior pede atualização.
- O cliente envia texto por `POST /transcript/{uuid}`, lê `GET /state` e confirma cartões por `POST /choice`.
- O MCP em `/mcp` expõe `device.transcript`, `device.reply` e cinco ferramentas. Publicar ou habilitar o plugin por si só não cria uma assinatura. A pessoa deve pedir a assinatura na conversa do próprio Dot.
- As credenciais de callback ficam cifradas. A entrega usa o domínio oficial do receptor OpenAI, prova de controle do callback, assinatura de webhook, IDs idempotentes e fila persistente. Novas tentativas ocorrem nas chamadas subsequentes do cliente, incluindo suas consultas regulares de estado.

## Recursos

D1 guarda registros e entregas. O bucket R2 privado contém o APK assinado. Configure os segredos `DOT_OWNER_EMAIL`, `MOBILE_TOKEN`, `EVENT_KEY_HEX` e `RELEASE_TOKEN` no serviço de hospedagem. Cada credencial tem uma finalidade distinta. A porta de distribuição aceita somente o APK da versão fixa, verifica seu SHA-256 e impede substituir um arquivo publicado por conteúdo divergente.

## Verificar

Com Node.js 22.13 ou superior, rode `node --test tests/bridge.test.mjs`. O teste usa SQLite local, uma conta de teste e um receptor de eventos simulado. Para reproduzir a aplicação, instale as dependências, gere as migrações ao modificar o esquema e use o fluxo de compilação e publicação do Sites.
