# Third-party notices

Meu Dot is built on the following projects. Components marked *bundled* ship inside this repository or inside the release binaries; the others are fetched at build time.

| Component | Version | License | Where |
| --- | --- | --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.5 | Apache-2.0 | Firmware toolchain and runtime (in the firmware binary) |
| [Waveshare ESP32-S3-Touch-AMOLED-1.75C BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75c) | 3.0.0 | Apache-2.0 | Firmware, fetched by the component manager |
| Espressif components (`esp_codec_dev`, `esp_lcd_co5300`, `esp_lvgl_adapter`, `button`, …) | see `firmware/dependencies.lock` | Apache-2.0 | Firmware, fetched by the component manager |
| [LVGL](https://github.com/lvgl/lvgl) | 9.5.0 | MIT | Firmware, fetched by the component manager |
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | `d1be6fde` | MIT | Android native library (bundled in the APK) |
| Whisper `ggml-base-q5_1` model | — | MIT | Downloaded by the app on request, SHA-256 checked |
| [Java-WebSocket](https://github.com/TooTallNate/Java-WebSocket) | 1.6.0 | MIT | Android (bundled in the APK) — `android/assets/Java-WebSocket-LICENSE.txt` |
| [msgpack-java](https://github.com/msgpack/msgpack-java) | 0.9.10 | Apache-2.0 | Android (bundled in the APK) — `android/assets/MessagePack-LICENSE.txt` |
| [SLF4J](https://www.slf4j.org) | 2.0.16 | MIT | Android (bundled in the APK) — `android/assets/SLF4J-LICENSE.txt` |
| [Inter](https://rsms.me/inter/) | 4.x variable | SIL OFL 1.1 | Firmware, app and images (bundled) — `art/Inter-OFL.txt` |
| [Next.js](https://nextjs.org), React, [Drizzle ORM](https://orm.drizzle.team), [shadcn/ui](https://ui.shadcn.com) and the other packages in `bridge/package.json` | see `bridge/package-lock.json` | MIT / Apache-2.0 | Bridge |
| [org.json](https://github.com/stleary/JSON-java) | 20250517 | Public domain | Host tests only |

[rsms/esp32-dot](https://github.com/rsms/esp32-dot) inspired this project. No code from it is included. It declares no license, so this repository only links to it.

"ChatGPT", "Dot" and "OpenAI" are trademarks of OpenAI. "Waveshare" is a trademark of Waveshare Electronics. This project is not affiliated with either company.
