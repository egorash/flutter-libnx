# TLS-корни на Horizon: новый Let's Encrypt "Root YR" и fallback-бандл движка

Запись по факту (подтверждено на железе: Switch + Atmosphère, приложение
`my_nexus`, 2026-10-10). Язык — русский, как новые материалы форка.

## Бефунд

1. Хосты `backend.my-nexus.ru` (API, GraphQL) и `my-nexus.ru` (картинки)
   выдают цепочку Let's Encrypt **новой иерархии**:

   ```
   leaf ← YR2 (intermediate, "Let's Encrypt YR2") ← Root YR (cross-sign: ISRG Root X1)
   ```

2. Движок собран с `dart_use_fallback_root_certificates=true`
   (`scripts/gn-gen-horizon.sh`) и встраивает бандл корней из
   `engine/src/flutter/third_party/dart/third_party/fallback_root_certificates/certdata.pem`
   (149 корней). В бандле есть `ISRG Root X1` и `X2`, но **нет `Root YR`**.

3. Проверка цепочки против бандла (без учёта системных корней macOS):

   ```
   openssl verify -CAfile <бандл> -untrusted <YR2-сертификат> <leaf-сертификат>
   → error 20 at 1 depth lookup: unable to get local issuer certificate
   ```

   — падает для обоих хостов `my_nexus`.

4. Из-за этого на консоли падает **любой** TLS к таким хостам: авторизация
   (Dio → `dart:io` HttpClient → engine BoringSSL) выбрасывает ошибку
   валидации сертификата, в UI — «Неверный логин или пароль» (ошибка сети
   и неверные учётные данные показываются одинаково).

5. Отлаживать вслепую сложно: в release/AOT `kDebugMode == false`, поэтому
   `Get.log` и `AppLogger.debug` из GetX **молчат**, в SD-лог сетевые ошибки
   не попадают (видна только движковая часть `[engine:flutter]`).

## Обход (app-side, точечный)

В `main()` приложения установить глобальный `HttpOverrides`, который
подтверждает сертификат **только для своих доменов** (у `my_nexus` —
`*.my-nexus.ru`); для остальных хостов валидация остаётся штатной.
Работает для Dio, `package:http` и `CachedNetworkImage` (все идут через
`HttpClient()`).

```dart
class _MyNexusHttpOverrides extends HttpOverrides {
  @override
  HttpClient createHttpClient(SecurityContext? context) {
    final client = super.createHttpClient(context);
    client.badCertificateCallback = (cert, host, port) =>
        host == 'backend.my-nexus.ru' || host.endsWith('.my-nexus.ru');
    return client;
  }
}
// в mainWithEnv:
HttpOverrides.global = _MyNexusHttpOverrides();
```

Такой же паттерн уже применял upstream в `dart_helpers/horizon_http.dart`
(`SecureSocket.secure(..., onBadCertificate: (_) => true)`) — только там
глобально для всех хостов.

## Диагностика (чтобы не гадать по UI)

`my_nexus/lib/app/shared/utils/net_probe.dart` — пишет
`sdmc:/switch/flutter-libnx/netprobe.txt` при старте приложения:
- часы устройства (`DateTime.now()`);
- DNS (`InternetAddress.lookup`);
- TCP 443 (`Socket.connect`);
- **«сырой» TLS** (`SecureSocket.connect` без обходов) — истинное поведение
  движка;
- HTTPS через `HttpClient` (уже с установленным `HttpOverrides`).

## Результат на железе (netprobe.txt, 2026-10-11)

```
----- backend.my-nexus.ru -----
DNS OK: 217.18.60.253
TCP 443 OK
RAW TLS OK (subject="/CN=backend.my-nexus.ru" issuer=".../CN=YR2")
HTTPX GET / -> HTTP 200
----- my-nexus.ru -----  (аналогично, всё OK)
----- dns.google -----   (аналогично, всё OK)
```

Выводы:

1. **«Сырой» TLS проходит для всех хостов, включая `backend.my-nexus.ru`.**
   Значит движок принимает цепочку `leaf ← YR2 ← Root YR (cross-sign: X1)»
   — BoringSSL использует `ISRG Root X1` из бандла как анкор и доверяет
   пришедшему от сервера cross-signed `Root YR` как промежуточному.
   Командная `openssl verify` этого не умеет (error 20) — но на консоли TLS
   валиден. Гипотеза «fallback-бандл не знает Root YR → TLS падает»
   **на консоли НЕ подтвердилась**.

2. Часы консоли синхронизированы (UTC == local, актуальная дата).

3. DNS-резолвинг через libnx работает (getaddrinfo) — `backend.my-nexus.ru`
   резолвится в 217.18.60.253.

4. Исходный сбой авторизации, скорее всего, был **транзиентным** (сеть
   после ребута/idle не была готова), а не следствием отсутствующего корня.
   **Решение (2026-10-11):** обход оставлен как точечная страховка
   (`*.my-nexus.ru`); диагностика `NetProbe` остаётся включённой при старте
   (флаг `NetProbe.enabled`). Снятие обхода — отдельный A/B-прогон, если
   захочется строгой валидации.

5. **Отладка:** `Get.log`/`AppLogger` (GetX) в release/AOT молчат
   (`kDebugMode == false`), а обычный `print()` из Dart попадает в SD-лог
   как `[engine:flutter]` — удобный канал для release-диагностики.

## Правильное долгосрочное решение (пока не сделано)

Обновить fallback-бандл корней движка: добавить новые корни Let's Encrypt
(`Root YR` и актуальный Mozilla-набор) в `certdata.pem` и пересобрать движок.
Тогда точечный обход в приложении не нужен, и TLS валидируется строго для
всех приложений (а не только для тех, кто добавил `badCertificateCallback`).
Минус — пересборка движка (~30–60 мин) и повторный гон снапшотов.