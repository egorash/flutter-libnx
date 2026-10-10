# План: плагины для приложения `my_nexus`

Статус: актуальный план. Дата: 2026-10-11.
Обновления: Фаза 0 частично пройдена на железе (старт/логин работают);
`app_links` **пропущен** (ошибка ловится приложением, deep links не
используются); приоритеты смещены на остальные плагины; null-check
исключение после логина — баг в коде приложения, не плагин.

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
| `app_links` | 6.4.1 | `com.llfbandit.app_links/messages` + `/events` | `deep_link_service.dart` | **старт — пропущен** (ошибка ловится, см. Фазу 1) |
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

**Подтверждено на железе (2026-10-10/11, итог Фазы 0):** приложение
стартует, сплэш и UI работают (60 fps), авторизация проходит (после
точечного TLS-обхода для `*.my-nexus.ru` — см. `docs/horizon-tls-roots.md`).
В SD-логе единственный плагинный шум — `com.llfbandit.app_links/events`
(`MissingPluginException` на `listen`), ловится приложением. Отдельно
поймано Dart-исключение «Null check operator used on a null value» на пути
после логина — это **не плагин**, а баг в коде `my_nexus`.

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

### Фаза 0 — диагностика на железе (частично выполнена 2026-10-10/11)

Итог на консоли (сборка `examples/my_nexus/my_nexus.nro`, режим приложения):

- Старт, сплэш, UI — работают (60 fps); авторизация проходит после
  точечного TLS-обхода для `*.my-nexus.ru` (см. `docs/horizon-tls-roots.md`).
- Единственный плагинный шум в SD-логе — `com.llfbandit.app_links/events`
  (`MissingPluginException` на `listen`); приложение ловит, не фатально.
- `path_provider`/`shared_preferences`/`hive` на старте работают
  (Hive-кэш и хранилища инициализируются).
- Поймано Dart-исключение «Null check operator used on a null value» после
  логина — баг в коде `my_nexus`, не плагин; разбирается отдельно.
- **Не проверено на железе:** карта, фото, web-view, внешние ссылки.
  Это продолжение Фазы 0: перед реализацией каждого элемента Фазы 3 —
  сначала прогнать сценарий на текущей сборке и зафиксировать реальные
  каналы/исключения.

### Фаза 1 — инфраструктура эмбеддера + скипы

1. **Инфраструктура (обязательно перед любым новым хендлером):**
   - Реестр каналов: вынести «канал → хендлер» из `if`-цепочек
     `plugins_horizon.cpp` в map — добавление плагина одной строкой.
   - EventChannel-помощник: общий ответ `success` на `listen`/`cancel`
     (Standard-кодек), чтобы стримы плагинов не сыпали ошибками
     (сейчас ни один EventChannel не обслуживается).
2. **`app_links` — ПРОПУЩЕН (решение 2026-10-11).** Ошибка ловится
   приложением (`try/catch` + `onError`), deep links не используются; upstream
   относит `app_links` к плагинам без осмысленной Switch-аналогии (группа C,
   `docs/target-apps.md`). Вернуться при появлении реальных deep-link фич
   (тогда: `getInitialLink`/`getLatestLink` → `null`, EventChannel → `success`
   без событий).
3. **`connectivity_plus` / `package_info_plus` — игнор (принцип C).**
   Прямых вызовов в коде нет, Фаза 0 вызовы не показала. Делать, только
   если повторится иначе.

**Критерий:** в эмбеддере есть реестр каналов и EventChannel-помощник;
стартовый лог без `MissingPluginException` от скипнутых плагинов (они не
дергаются).

### Фаза 2 — app-агностичный охват

**`url_launcher`** — уже реализован; проверить на железе под 6.3.x (клик
по внешней ссылке из приложения):
- `canLaunch` args `{url}`, `launch` args `{url, useSafariVC, useWebView,
  enableJavaScript, enableDomStorage, universalLinksOnly, headers}` →
  `bool`; `closeWebView` → void. Реализация через системный браузер-апплет
  (`webPageCreate`+`webConfigShow`), только `http`/`https`.
- Покрыть оба пути вызова: `launchUrl(uri, mode: externalApplication)` и
  `url_launcher_string` (метод на канале один и тот же).
- `mailto:`/`tel:` → честный `false`.

**Критерий:** клик по ссылке открывает системный браузер и возврат в
приложение работает; формат args 6.3.x совпадает с текущим хендлером.

### Фаза 3 — фичи `my_nexus` (порядок кандидатов ниже, уточняется)

Приоритет по ценности для `my_nexus` (согласуется с заказчиком):

1. **`geolocator`** (карта путешествий, `travels_map_controller.dart`).
   GPS-железа нет. Хендлер A (C++) + EventChannel-помощник из Фазы 1:
   - `checkPermission`/`requestPermission` → `LocationPermission.denied`
     (или `whileInUse`, если решим «разрешать»);
   - `isLocationServiceEnabled` → `false`;
   - `getCurrentPosition` → `PlatformException('denied', ...)`;
   - EventChannel `geolocator_updates`/`service_updates` — `success` на
     `listen`/`cancel`, без событий.
   Критерий: экран карты не крашит; отсутствие GPS показано понятно.
2. **`image_picker` + `image_cropper`** (фото в визарде, `strapi_media`).
   Камеры нет; конвенция «import-папка» (как `file_picker`):
   `sdmc:/switch/flutter_apps/my_nexus/import/`.
   - (6a) Dart-шim B: `ImagePickerPlatform.instance` → берёт файл из
     import-папки; `ImageCropperPlatform.instance` → no-op (исходный файл).
   - (6b) честная ошибка `PlatformException('unimplemented')`.
   Критерий: добавление фото либо работает через import-папку, либо
   показывает понятную ошибку, не роняя визард.
3. **`webview_flutter`** (музыкальные заметки, `note_music_section.dart`).
   Встроенного web-view на Switch нет (системный браузер — отдельный
   апплет, `docs/target-apps.md`). Варианты:
   - (3a) Dart-шim B: `WebViewPlatform.instance` — открывать URL системным
     браузером (через наш `url_launcher`) + заглушка-widget;
   - (3b) честная ошибка: `WebViewController()` → `PlatformException` —
     дешевле и честнее (Pigeon-API webview объёмное).
   Критерий: открытие заметки с музыкой/треком не крашит приложение.

**Критерий:** карта, фото и музыкальные заметки не роняют приложение;
неподдерживаемое показывается явной ошибкой.

### Фаза 4 — вне области (фиксируем явно)
- Реальный встроенный WebView (нет движка как view).
- Камера, GPS-железо, биометрия.
- Видео: не для `my_nexus` (media_kit в нём не используется), общая тема
  отдельно в `target-apps.md`.

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
  (сценарий `app_links` снят — плагин пропущен)
- Регресс `ui_app` и `aot_poc` после правок эмбеддера.

---

## 7. Риски и открытые вопросы

1. **Версии протоколов.** `url_launcher` 6.x, `image_picker` Pigeon — форматы
   проверять по `~/.pub-cache`, а не по памяти. Хрупкость Dart-шимов (B)
   к смене версии platform-interface.
2. **app_links** — пропущен (решение 2026-10-11): ошибка ловится
   приложением, deep links не используются; вернуться при появлении фичи.
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
