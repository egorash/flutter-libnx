# План: плагины для приложения `my_nexus`

Статус: черновик плана. Дата: 2026-10-10.

Дополняет `docs/target-apps.md` (общая классификация A–D) — здесь конкретный,
приоритизированный план для реального приложения `my_nexus`
(`/Users/egorshepelev/Documents/dev/flutter/my_nexus`, сборка уже проходит:
`examples/my_nexus/my_nexus.nro`, 61 МБ).

---

## 0. Принципы

1. **Приложение остаётся неизменным.** Правки — только в эмбеддере
   (`embedder/`) и в генерируемом регистранте (`examples/<app>/generated/
   horizon_main.dart`). Файлы самого проекта не трогаем.
2. **Заглушка обязана быть честной.** Либо осмысленный ответ, либо явная
   ошибка (`PlatformException`/`MissingPluginException`) — никакого
   молчаливого `return false`/`0`.
3. **Сначала — app-агностичные плагины** (встречаются почти в любой Flutter-
   программе), потом — узкие фичи `my_nexus`.
4. **Сначала железо.** Реальный список дергаемых каналов даёт запуск на
   консоли + `scripts/log-listener.sh`, а не чтение кода.

---

## 1. Уже реализовано в эмбеддере

`embedder/src/engine/plugins_horizon.cpp`:

| Плагин | Канал | Кодек |
|---|---|---|
| `path_provider` | `plugins.flutter.io/path_provider` | Standard |
| `shared_preferences` | `plugins.flutter.io/shared_preferences` | Standard |
| `flutter_secure_storage` | `plugins.it_nomads.com/flutter_secure_storage` | Standard |
| `url_launcher` | `plugins.flutter.io/url_launcher` | Standard |
| `file_picker` | `miguelruivo.flutter.plugins.filepicker` | **JSON** (не Standard!) |

Плюс: `textinput_horizon.cpp` (системная клавиатура `swkbd`),
`media_kit_video_horizon.cpp` (видео-текстуры media_kit), FFI-мост sqlite3
(`static_libraries_horizon.cpp`).

Данные приложения: `/switch/flutter_apps/<app-id>/` (на Switch —
`sdmc:/switch/flutter_apps/my_nexus/`). `<app-id>` = `FLUTTER_LIBNX_APP_ID`
(= `TARGET` из Makefile).

---

## 2. Что использует `my_nexus`

| Плагин | Версия (lock) | Канал / hook | Где в коде | Когда дергается |
|---|---|---|---|---|
| `path_provider` | ✅ | — | `hive_flutter`, кэш картинок | старт |
| `shared_preferences` | ✅ | — | `auth_service`, `colors.dart`, `user_session_store` | старт |
| `hive` / `hive_flutter` | pure Dart | через `path_provider` | `main.dart`, `cache_metadata*` | старт |
| `url_launcher` | 6.3.2 | `plugins.flutter.io/url_launcher` | `note_video_section`, `social_row`, `track_button`, `travel_points_list`, `link_card` | по действию |
| `app_links` | 6.4.1 | `com.llfbandit.app_links/messages` + `/events` | `deep_link_service.dart` | **старт** (ошибка ловится) |
| `geolocator` | 14.x | `flutter.baseflow.com/geolocator` (+ `..._updates`, `..._service_updates`) | `travels_map_controller.dart` | лениво (карта) |
| `image_picker` | 1.2.3 | Pigeon; hook `ImagePickerPlatform.instance` | `packages/strapi_media/.../strapi_media_client.dart` | лениво (фото) |
| `image_cropper` | 12.2.1 | hook `ImageCropperPlatform.instance` | `packages/strapi_media/.../image_editor.dart` | лениво (кроп) |
| `webview_flutter` | 4.10+ | Pigeon; hook `WebViewPlatform.instance` | `note_music_section.dart`, `track_section.dart` | лениво (web-view) |
| `connectivity_plus` | 6.1.x | `dev.fluttercommunity.plus/connectivity` | прямых вызовов в коде нет | — |
| `package_info_plus` | 10.2.x | `dev.fluttercommunity.plus/package_info` | прямых вызовов в коде нет | — |
| `sqflite`, `jni`, `jni_flutter` | — | — | не используются приложением (Hive вместо sqflite) | — |

**Вывод по старту.** Стартовый путь (`main.dart` → `Hive.initFlutter` →
`CacheMetadataService` → `MediaPlugin.init` → `NetworkService` → `ColorTheme`
→ `DeepLinkService`) опирается на `path_provider` + `shared_preferences` +
`hive` (уже покрыты) и на `app_links` (ошибка `getInitialLink` и
`uriLinkStream` ловится в `try/catch` / `onError`, только лог). То есть
**приложение, вероятно, уже стартует** — падения будут точечные, в фичах.

---

## 3. Две площадки реализации

### A. C++-хендлер канала в эмбеддере
Как `path_provider`/`shared_preferences`. Годится для методов с простым
Standard/JSON-протоколом. Регистрация — в `flutter_libnx_handle_plugin_message`
(`plugins_horizon.cpp`), плюс строка диспетчеризации в `main.cpp` (если канал
новый). Плюс: ноль изменений в Dart, работает для любой версии плагина, пока
совпадает протокол.

### B. Dart-регистрант (подмена platform-interface)
`build-dart-app.sh` уже генерирует `generated/horizon_main.dart` (сейчас —
только для `file_picker`: `FilePickerIO.registerWith()`). Расширяем: генерируем
импорт шима + присвоение `XPlatform.instance = ...`. Сгенерированный файл
компилируется в пакете приложения, поэтому может импортировать
`package:image_picker_platform_interface/...` и т. п. (они есть в
`package_config.json` как транзитивные зависимости). Плюс: можно реализовать
логику на Dart, не трогая C++. Минус: привязка к версии platform-interface.

### C. Ничего / игнор
Для каналов, которые приложение реально не вызывает (`connectivity_plus`,
`package_info_plus`), — не делать ничего, пока запуск не покажет обратное.

---

## 4. Фазы

### Фаза 0 — диагностика на железе (предусловие)
1. `scripts/nxlink-upload.sh --switch-ip <ip> --example my_nexus`.
2. `scripts/log-listener.sh` — поймать реальный вывод.
3. Зафиксировать: стартует ли UI; какие `MissingPluginException` /
   `PlatformException` реально прилетают и на каком экране.

**Критерий:** список фактически дергаемых каналов и точка первого падения.
Это уточняет приоритеты ниже (возможно, `connectivity_plus` не понадобится
вообще).

### Фаза 1 — старт-критичное, тривиально (низкий риск)
1. **`app_links`** — хендлер A на оба канала:
   - `com.llfbandit.app_links/messages`: `getInitialLink`/`getLatestLink` →
     `null`, `success` (Standard).
   - `com.llfbandit.app_links/events`: EventChannel — отвечать `success` на
     `listen`/`cancel`, события не шлём.
   Цель: убрать шум в логе и «неопределённое» поведение стрима.
2. **`connectivity_plus` / `package_info_plus`** — только если Фаза 0
   показала вызовы. `connectivity`: `check` → `['none']`/`wifi`;
   `package_info`: `getAll`/`get` → значения из NACP/констант.

**Критерий:** чистый лог на старте, оффлайн-сценарий не падает.

### Фаза 2 — app-агностичный охват (высокий приоритет)
3. **`url_launcher`** — уже есть; **проверить под 6.3.2**:
   - `canLaunch` args `{url}`, `launch` args `{url, useSafariVC, useWebView,
     enableJavaScript, enableDomStorage, universalLinksOnly, headers}` →
     `bool`; `closeWebView` → void. Реализация через системный браузер-апплет
     (`webPageCreate`+`webConfigShow`), только `http`/`https`.
   - Приложение вызывает `launchUrl(uri, mode: externalApplication)` и
     `url_launcher_string` — покрыть оба пути (метод на канале один и тот же).
   **Критерий:** клик по ссылке открывает системный браузер; `mailto:`/`tel:`
   отвечают `false` (честно).

### Фаза 3 — фичи `my_nexus`
4. **`webview_flutter`** — webview как встроенного виджета на Switch нет
   (системный браузер — отдельный апплет, см. `target-apps.md`). Варианты:
   - (3a) Dart-шim B: `WebViewPlatform.instance` — реализация, которая при
     создании контроллера открывает URL системным браузером (через наш
     url_launcher) и рисует заглушку-widget. Экраны `note_music_section`/
     `track_section` не падают.
   - (3b) честная ошибка: `WebViewController()` возвращает `PlatformException`
     «WebView nicht unterstützt» — если приложение умеет это показать.
   **Критерий:** открытие заметки с музыкой/треком не крашит приложение.

5. **`geolocator`** — GPS-железа нет. Хендлер A:
   - `checkPermission`/`requestPermission` → `LocationPermission.denied`
     (или `whileInUse`, если решим «разрешать»).
   - `isLocationServiceEnabled` → `false`.
   - `getCurrentPosition` → `PlatformException('denied', ...)`.
   - EventChannel `geolocator_updates`/`service_updates` — `success` на
     `listen`/`cancel`, без событий.
   Дальше решить по факту: либо приложение корректно показывает «нет
   геолокации», либо (опция) отдавать фиксированную/ручную позицию.
   **Критерий:** экран карты не крашит; отсутствие GPS отображается понятно.

6. **`image_picker` + `image_cropper`** (`strapi_media`: загрузка/кроп фото) —
   камеры нет; на Switch уже есть конвенция «import-папка» (как `file_picker`).
   Варианты:
   - (6a) Dart-шim B: `ImagePickerPlatform.instance` → реализация, берущая
     файл из `/switch/flutter_apps/<id>/import/` (через наш канал/`file_picker`);
     `ImageCropperPlatform.instance` → no-op, возвращающий исходный файл.
   - (6b) честная ошибка `PlatformException('unimplemented')`.
   **Критерий:** добавление фото либо работает через import-папку, либо
   показывает понятную ошибку, не роняя визард.

### Фаза 4 — вне области (фиксируем явно)
- Реальный встроенный WebView (нет движка как view).
- Камера, GPS-железо, биометрия.
- Видео: не для `my_nexus` (media_kit в нём не используется), общая тема
  отдельно в `target-apps.md`.

---

## 5. Общая инфраструктура (по ходу)

1. **Реестр каналов.** Вынести таблицу «канал → хендлер» из `if`-цепочек в
   `plugins_horizon.cpp` в один реестр (map), чтобы добавление плагина было
   одной строкой.
2. **EventChannel-помощник.** Сейчас EventChannel нигде не обслуживается.
   Нужен общий ответ `success` на `listen`/`cancel` (Standard-кодек), чтобы
   стримы плагинов не сыпали ошибками.
3. **Регистрант.** Расширить `build-dart-app.sh`: помимо детекта `file_picker`
   — детект `image_picker`/`image_cropper`/`webview_flutter` и генерация
   соответствующих присвоений `*.instance`, плюс включение шим-файлов из
   `dart_helpers/` (или отдельного `dart_helpers/horizon_plugins/`) в
   `generated/`.
4. **Телеметрия.** В лог — имя каждого необработанного канала (сейчас тихо
   возвращается `false` → `MissingPluginException`). Облегчает Фазу 0 и
   последующую отладку.

---

## 6. Тестирование

- Сборка: `build-dart-app.sh --example my_nexus --project <путь>` +
  `rebuild-all.sh my_nexus`.
- Железо: `nxlink-upload.sh` + `log-listener.sh`.
- Чек-лист сценариев: старт/сплэш/логин → домашний экран → карта без GPS →
  заметка с web-view → визард с фото → открытие внешней ссылки.
- Регресс `ui_app` и `aot_poc` после правок эмбеддера.

---

## 7. Риски и открытые вопросы

1. **Версии протоколов.** `url_launcher` 6.x, `image_picker` Pigeon — форматы
   проверять по `~/.pub-cache`, а не по памяти. Хрупкость Dart-шимов (B)
   к смене версии platform-interface.
2. **app_links на старте.** Возможно, ошибку уже ловит приложение, и Фаза 1
   не обязательна — решит Фаза 0.
3. **`WebViewPlatform` API объёмный** (Pigeon, много методов) — шим B может
   быть нетривиальным; возможно, дешевле честная ошибка (3b).
4. **`strapi_media`** — локальный пакет; при Dart-шимах убедиться, что его
   вызовы `ImagePicker()`/`ImageCropper()` идут через `*.instance`.
5. **Неизвестные каналы.** Фаза 0 может открыть каналы, которых нет в таблице.

---

## 8. Артефакты

- Правки: `embedder/src/engine/plugins_horizon.cpp` (+ реестр, EventChannel),
  возможно `media_kit_video_horizon.cpp` не трогаем.
- Генерация: `scripts/build-dart-app.sh` (регистрант), новые шимы в
  `dart_helpers/`.
- Документы: этот файл, `docs/macos-migration.md` (контракт регистранта),
  `docs/target-apps.md` (статусы).
