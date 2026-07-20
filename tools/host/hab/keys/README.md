# HAB test key — dev-test-only

`bootloader_test_hab_key.pem` / `bootloader_test_hab_crt.pem` — самоподписанный
RSA-2048 ключ, сгенерированный для Фазы 5 (`firmware/bootloader/PLAN.md`),
чтобы `hab_bootloader_release.yaml` реально собирал **подписанный** HAB-образ
(`flags=0x08`, схема HAB4 NOCAK — один ключ на CSF и на данные) вместо
unsigned-заглушки.

Тот же принцип, что и у тестового ключа MCUboot
(`sdk/middleware/mcuboot_opensource/root-ec-p256.pem`):
ключ публичный, не секрет, годится только чтобы механизм подписи
проверялся end-to-end на HAB Open чипе (который не отвергает образ даже при
несовпадении подписи с fuses).

**Для серийного производства этот ключ использовать нельзя.** Реальный
production-ключ — отдельная SRK-церемония (генерация вне этого репозитория,
приватная часть — в HSM/vault, публичный хэш — в OTP fuses при переводе чипа
в HAB Closed). Общая теория — [docs/bootloader/HAB_GUIDE.md](../../../../docs/bootloader/HAB_GUIDE.md)
§5, §8; конкретный план с командами (`nxpcrypto pki-tree hab` и далее) —
[firmware/bootloader/SIGNING_CEREMONY.md](../../../../firmware/bootloader/SIGNING_CEREMONY.md).

## Как эти файлы используются в `hab_bootloader_release.yaml`

- `bootloader_test_hab_crt.pem` (сертификат, публичный) — ставится в key store
  HAB командой `InstallNOCAK`.
- `bootloader_test_hab_key.pem` (приватный ключ) — используется на хосте,
  во время сборки, чтобы подписать CSF (`AuthenticateCSF`) и сам образ
  (`AuthenticateData`).

Подробный разбор, что делает каждая команда CSF-секции (`Header`/`InstallNOCAK`/
`AuthenticateCSF`/`AuthenticateData`) и чем NOCAK отличается от полной
production-схемы с SRK-таблицей — [docs/bootloader/HAB_GUIDE.md](../../../../docs/bootloader/HAB_GUIDE.md)
§5.1.

Сгенерирован:

```bash
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 7300 \
  -keyout bootloader_test_hab_key.pem \
  -out bootloader_test_hab_crt.pem \
  -subj "/CN=TFT bootloader TEST HAB key - NOT FOR PRODUCTION/O=dev-test-only"
```
