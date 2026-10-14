# Инфраструктура embedder'а (Horizon)

Как устроен «фундамент», на котором держатся плагины, стримы, регистрант и
иконка `.nro`. Гайд для тех, кто добавляет канал/плагин или разбирается,
почему `MissingPluginException`.

**Аудитория:** разработчик embedder'а / тот, кто портирует Flutter-приложение.
**Канонический пошаговый чеклист** «добавить плагин» живёт в
`examples/plugin_template/README.md` — здесь разобраны механизмы, на которые
этот чеклист опирается.

Смежные документы — в разделе «[Смежное](#9-смежное)».

---

## 1. Поток платформенного сообщения

Flutter на хосте порождает `FlutterPlatformMessage` (канал + бинарный payload +
`response_handle`) и отдаёт его платформенному коллбэку, зарегистрированному
через `FlutterEngineRun`. В нашем embedder'е этот коллбэк живёт в **примере**
(`examples/<app>/source/main.cpp`) и разводит сообщения по группам:

```
FlutterPlatformMessage
        │
        ▼
examples/<app>/source/main.cpp   (Kanal-Router)
        │
        ├─ flutter_libnx/system                    // Батарея (пример-специфичный)
        ├─ flutter_libnx_handle_textinput()        // flutter/textinput
        ├─ flutter_libnx_handle_media_kit_video()  // media_kit_video
        ├─ flutter_libnx_handle_plugin_message()   // ← все «общие» плагины
        └─ respond(nullptr)                        // чужие каналы: пустой ответ
```

Ключевое разделение:

- **Общие плагины** (`path_provider`, `shared_preferences`, `file_picker`,
  `geolocator`, …) — в embedder'е (`embedder/src/engine/plugins_horizon.cpp`).
  Правка нужна **один раз** и работает во всех приложениях.
- **Специфичные для приложения** каналы (`flutter_libnx/system` — батарея,
  `flutter/textinput`, `media_kit_video`) подключаются в `main.cpp` конкретного
  примера.

`respond(nullptr)` в конце — не «проглатывание». Пустой ответ на платформенной
стороне превращается на стороне Dart в `MissingPluginException` — честное «этот
канал здесь не реализован». Именно это ловят `LogUnknownChannelOnce` и
диагностика (см. §8).

Все хендлеры выполняются на **платформенном потоке** (главной петле движка).
Никакой параллельности с Dart, поэтому в хендлерах нет блокировок и нет нужды
в них: состояние (например, загруженные настройки) можно держать в обычных
глобалах.

---

## 2. Реестр каналов

Раньше диспетчер был цепочкой `if (strcmp(channel, ...))`. Теперь это две хэш-таблицы
(`embedder/src/engine/plugins_horizon.cpp`):

```cpp
using RawHandler    = bool (*)(FlutterEngine, const FlutterPlatformMessage*);
using MethodHandler = bool (*)(FlutterEngine, const FlutterPlatformMessage*,
                               const std::string& method, const StdValue& args);

struct ChannelRegistry {
  std::unordered_map<std::string, RawHandler>    raw;     // свой кодек
  std::unordered_map<std::string, MethodHandler> method;  // StandardMethodCodec
};

ChannelRegistry MakeRegistry() {
  ChannelRegistry r;
  // Каналы со своим форматом (декодируют сообщение сами).
  r.raw["miguelruivo.flutter.plugins.filepicker"]              = HandleFilePicker;
  r.raw["flutter.baseflow.com/geolocator_updates"]             = HandleGeolocatorUpdates;
  r.raw["flutter.baseflow.com/geolocator_service_updates"]     = HandleGeolocatorServiceUpdates;
  // Каналы на StandardMethodCodec: method + args, ответ через EncodeStd*.
  r.method["plugins.flutter.io/path_provider"]                 = HandlePathProvider;
  r.method["plugins.flutter.io/shared_preferences"]            = HandleSharedPreferences;
  r.method["plugins.it_nomads.com/flutter_secure_storage"]     = HandleSecureStorage;
  r.method["plugins.flutter.io/url_launcher"]                  = HandleUrlLauncher;
  r.method["flutter.baseflow.com/geolocator"]                  = HandleGeolocator;
  r.method["plugins.flutter.io/image_picker"]                  = HandleImagePicker;
  return r;
}

const ChannelRegistry& GetRegistry() {
  static const ChannelRegistry kRegistry = MakeRegistry();  // einmalig
  return kRegistry;
}
```

Диспетчер (`flutter_libnx_handle_plugin_message`) делает лукап и вызов:

```cpp
const ChannelRegistry& reg = GetRegistry();
const std::string channel(message->channel);

if (auto raw = reg.raw.find(channel); raw != reg.raw.end())
  return raw->second(engine, message);            // свой кодек: сам ответит

auto method = reg.method.find(channel);
if (method == reg.method.end()) {
  LogUnknownChannelOnce(channel);                 // LOG_WARN, einmal je Kanal
  return false;                                   // → MissingPluginException
}

std::string name; StdValue args;
if (!DecodeStdMethodCall(message->message, message->message_size, &name, &args)) {
  LOG_WARN("%s: Nachricht nicht dekodierbar (%zu Bytes)", channel.c_str(),
           message->message_size);
  return false;
}
return method->second(engine, message, name, args);
```

**`raw` vs `method` — как выбрать.** Определяется протоколом *самого пакета*, а
не вкусом:

- `method` — пакет шлёт `StandardMethodCodec` (бинарный): почти все плагины
  (`path_provider`, `shared_preferences`, `image_picker`, …). Вы получаете уже
  разобранные `method` и `args`.
- `raw` — у пакета свой формат. `file_picker` на «Linux» (а мы представляемся
  Linux'ом, `file_picker_io.dart:7`) говорит на `JSONMethodCodec`; EventChannel'ы
  (`geolocator_updates`) несут `StandardMethodCodec`, но с протоколом
  `listen`/`cancel` (§4). Такой хендлер декодирует payload **сам**.

**Контракт возвращаемого значения:**

| Возврат | Значение |
|---|---|
| `true` | Сообщение обработано (ответ отправлен *или* сознательно оставлен вызывающему) |
| `false` | Пустой ответ → на Dart-стороне `MissingPluginException` |

Возвращайте `false` **только осознанно** — когда «не реализовано» и есть
правильный ответ (см. §3).

### Как добавить канал (Путь A, кратко)

1. Написать функцию-хендлер (образец — `examples/plugin_template/source/handler_stub.cpp`)
   и скопировать её в `plugins_horizon.cpp`.
2. Добавить **одну строку** в `MakeRegistry()`:
   `r.method["<channel>"] = HandleXyz;` (или `r.raw` для своего формата).
3. Пересобрать: `bash scripts/rebuild-all.sh <example>`.
4. Dart-сторона: `MethodChannel('<тот же канал>')` с демо-вызовом.

Пошаговый чеклист и разбор «Путь B (Dart-шим)» — в
`examples/plugin_template/README.md`.

---

## 3. StandardMethodCodec и `StdValue`

Каналы `plugins.flutter.io/*` говорят не JSON'ом, а **StandardMethodCodec**:
`flutter_libnx/standard_message_codec.h`.

- Вызов метода = подряд два закодированных значения: имя (`String`) + аргументы.
- Ответ = байт-«конверт» (`0` = успех, `1` = ошибка) + значение(я).

Реализовано **сознательно узкое подмножество** `StdValue`:
`null`, `bool`, `int32/64`, `double`, `String`, `List`, `Map`. Типизированные
массивы (`Uint8List`, …) декодер **честно отвергает** (`false`), а не понимает
наполовину: «нужен `Uint8List` — сразу видно, где затык».

API:

```cpp
bool DecodeStdMethodCall(data, size, &method, &args);   // имя + аргументы
std::vector<uint8_t> EncodeStdSuccess(const StdValue& result);
std::vector<uint8_t> EncodeStdError(const std::string& code,
                                    const std::string& message);
```

Полный пример хендлера — `path_provider`:

```cpp
bool HandlePathProvider(FlutterEngine engine, const FlutterPlatformMessage* message,
                        const std::string& method, const StdValue& /*args*/) {
  const char* subdir = nullptr;
  if (method == "getTemporaryDirectory")            subdir = "tmp";
  else if (method == "getApplicationDocumentsDirectory") subdir = "documents";
  else if (method == "getApplicationSupportDirectory")   subdir = "support";
  // … Library/Downloads/Cache …
  else if (method == "getExternalStorageDirectory") {
    // Второго носителя на консоли нет — null это документированный ответ.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  } else {
    LOG_WARN("path_provider: unbekannte Methode '%s'", method.c_str());
    return false;  // leere Antwort -> MissingPluginException, ehrlich
  }

  const std::string path = BaseDir() + "/" + subdir;
  if (!EnsureDir(path)) {
    Respond(engine, message,
            EncodeStdError("io", "Verzeichnis nicht anlegbar: " + path));
    return true;
  }
  Respond(engine, message, EncodeStdSuccess(StdValue::String(path)));
  return true;
}
```

Два локальных хелпера в `plugins_horizon.cpp`:

- `Respond(engine, message, payload)` — безопасная отправка ответа
  (не делает ничего, если `response_handle == nullptr`).
- `BaseDir()` → `/switch/flutter_apps/<app-id>` (см. §7 про путь без `sdmc:`).

### Правило честности

Никаких «молчаливых» `false`/`0`/пустых строк вместо осмысленного ответа. Если
функции нет физически — так и скажите: `EncodeStdError(...)` или
`LOG_WARN + return false`. Молчание маскирует реальные ошибки в приложении.

---

## 4. EventChannel-хелпер

`embedder/include/flutter_libnx/event_channel.h` +
`embedder/src/engine/event_channel_horizon.cpp`.

Dart-`EventChannel` посылает на платформу `listen`/`cancel` и ждёт **поток**
событий. Хелпер инкапсулирует протокол и хранит состояние подписки:

```cpp
class EventChannel {
 public:
  explicit EventChannel(std::string name);   // название 1:1 как в Dart

  bool HandleMessage(FlutterEngine, const FlutterPlatformMessage*);  // listen/cancel
  bool IsListening() const;

  void SendEvent(FlutterEngine, const StdValue& data) const;         // успех-конверт
  void SendError(FlutterEngine, const std::string& code,
                 const std::string& detail) const;                   // onError в Dart
 private:
  std::string name_;
  bool listening_ = false;
};
```

Семантика:

- `HandleMessage` возвращает `true`, если `listen`/`cancel` распознаны и
  подтверждены; `false` — формат/метод не те (`Dart` увидит недоставляемость).
- `SendEvent` без активного слушателя **ничего не делает** — та же семантика,
  что у desktop-embedder'ов.

### Пример: geolocator (два стрима без событий)

```cpp
// Консоль не имеет системы геолокации, поэтому поток нечем питать:
// listen/cancel честно квитируются, событий не будет.
static EventChannel g_geolocator_updates(
    "flutter.baseflow.com/geolocator_updates");
static EventChannel g_geolocator_service_updates(
    "flutter.baseflow.com/geolocator_service_updates");

bool HandleGeolocatorUpdates(FlutterEngine engine,
                             const FlutterPlatformMessage* message) {
  return g_geolocator_updates.HandleMessage(engine, message);
}
```

Оба канала идут через `r.raw[...]` в реестре: их сообщения — это
`listen`/`cancel`, а не «метод + args», поэтому разбирает их сам хелпер.

### Как добавить новый EventChannel

1. Написать Dart-плагин, который слушает `EventChannel('«канал»')`.
2. Завести `static EventChannel g_x("«канал»")` и обёртку на
   `HandleMessage` (как у geolocator).
3. Строку `r.raw["«канал»"] = HandleXStream;` в `MakeRegistry()`.
4. Данные слать событиями: `g_x.SendEvent(engine, StdValue::…)` — но **только**
   если `g_x.IsListening()` не мешает вашей логике (сам `SendEvent` безопасен и
   без слушателя просто ничего не отправит).

---

## 5. Dart-шимы и регистрант

### Зачем шим

Плагины вида `image_picker`/`image_cropper`/`webview_flutter` живут за
**статическим синглтоном** `XPlatform.instance`. На Horizon нет устройства их
регистрации (нет `GeneratedPluginRegistrant` из нативного Android/iOS), поэтому
инстанс остаётся `null`, и пакет падает (например, `webview_flutter` — на
`instance!`). Шим подменяет инстанс:

```dart
// dart_helpers/horizon_plugins/shim_<пакет>.dart
class HorizonXPlatform extends XPlatform {
  // … честная реализация или PlatformException/MissingPluginException …
}

void registerHorizonShim() {
  XPlatform.instance = HorizonXPlatform();
}
```

**Маркер `// HORIZON_SHIM: READY`** — предохранитель: без него
`build-dart-app.sh` шим **не подключит**. Недоделанный шим не должен ломать
рантайм; незачотный шим оставляет в сгенерированном файле комментарий-заметку
(видно, что ещё не готово). Текущее состояние: `shim_image_picker.dart`,
`shim_image_cropper.dart`, `shim_webview_flutter.dart` — **все без READY**
(`image_picker` обслуживается C++-каналом, остальные ждут реализации).

### Генерация регистранта

`scripts/build-dart-app.sh` (секция «Horizon-Registrant») создаёт
`examples/<example>/generated/horizon_main.dart` и делает **его** точкой входа
для `gen_kernel_aot` вместо `lib/main.dart` проекта.

Алгоритм:

1. Прочитать имя пакета из `pubspec.yaml` (для `import 'package:<name>/main.dart'`).
2. Просканировать `.dart_tool/package_config.json`:
   - `file_picker` → `FilePickerIO.registerWith()`;
   - из списка `shim_pkgs` (`image_picker image_cropper webview_flutter`) — если
     пакет есть и его шим содержит `// HORIZON_SHIM: READY`, шим копируется в
     `generated/` и вызывается `registerHorizonShim()`; иначе — комментарий-заметка.
3. Если найден **хотя бы один** поддерживаемый пакет (`detected=1`), файл
   генерируется **всегда** — даже когда регистрировать пока нечего, чтобы
   заметки-комментарии показали, что ещё не подключено.
4. Регистрации идут **до** `app.main()` — только так статические инстансы
   успевают подмениться до первого обращения к ним.

Пример сгенерированного файла (`examples/my_nexus/generated/horizon_main.dart`):

```dart
// Von build-dart-app.sh erzeugt - nicht von Hand pflegen.
import 'package:my_nexus/main.dart' as app;

// image_picker: Shim vorhanden (dart_helpers/horizon_plugins/shim_image_picker.dart), aber nicht READY
// image_cropper: Shim vorhanden (…shim_image_cropper.dart…), aber nicht READY
// webview_flutter: Shim vorhanden (…shim_webview_flutter.dart…), aber nicht READY

void main() {
  app.main();
}
```

Когда шимов нет, но есть `file_picker`, в `main()` появится
`  FilePickerIO.registerWith();` перед `app.main();`.

> Файл помечен «nicht von Hand pflegen» — он перезаписывается при каждой
> сборке. Правки — только в шимах/скрипте.

**Путь B (создать шим):** полный чеклист — в
`examples/plugin_template/README.md`.

---

## 6. Иконка `.nro`

libnx встраивает в `.nro` JPEG 256×256, а devkitPro-Makefile примеров
подхватывает её сам — из `icon.jpg` (или `<TARGET>.jpg`) в каталоге примера.
`build-dart-app.sh` кладёт туда картинку из **самого Flutter-проекта**.

`pick_nro_icon(project, app)` ищет источник в таком порядке:

1. `assets/icons/logo.png` → `assets/logo.png` → `assets/app_icon.png` →
   `assets/icon.png`;
2. иначе — самая крупная Android-mipmap:
   `android/app/src/main/res/mipmap-{xxxhdpi,xxhdpi,xhdpi,hdpi,mdpi}/ic_launcher.png`.

Затем конвертирует в JPEG 256×256 в `examples/<example>/icon.jpg` первым
доступным конвертером:

| Конвертер | Команда |
|---|---|
| `sips` (macOS, штатный) | `sips -s format jpeg -z 256 256` |
| ImageMagick | `magick … -resize 256x256! -quality 90` |
| ImageMagick (legacy) | `convert … -resize 256x256! -quality 90` |
| Python + Pillow | `PIL.Image.resize((256,256), LANCZOS)` |

Если ни источника, ни конвертера нет — `.nro` собирается со **стандартной
иконкой**; это предупреждение в stderr, а **не** ошибка сборки.

Дальше Makefile примера видит `icon.jpg` и добавляет `--icon=$(APP_ICON)` к
`elf2nro` (плюс `.nacp` с именем/идентификатором, если `NO_NACP` не задан).

Артефакт `icon.jpg` — промежуточный и лежит рядом с примером (в `.gitignore`).

---

## 7. Идентификатор приложения и данные

Каждый пример компилируется с `-DFLUTTER_LIBNX_APP_ID="<target>"` (Makefile),
а `plugins_horizon.cpp` читает это как `kAppId`:

```
BaseDir()  →  /switch/flutter_apps/<app-id>
```

Туда ложатся: `tmp/`, `documents/`, `support/`, `cache/`, `library/`,
`downloads/`, `import/`, `shared_preferences.std`, `secure_storage.std`.

Путь **намеренно без префикса `sdmc:`** — hbmenu стартует с `sdmc` как
устройством по умолчанию, а POSIX-чистый путь спокойно переживает арифметику
пакета `path` (`p.join`, `p.normalize`, `isAbsolute`) в Dart.

---

## 8. Типичные ловушки

1. **`MissingPluginException` для незнакомого канала — ожидаемо.** Диспетчер
   логирует канал один раз (`LogUnknownChannelOnce`) и возвращает `false`. Это
   не баг, а список того, что ещё не реализовано. Пример из логов `my_nexus`:
   `app_links` вне реестра.
2. **Шим без `// HORIZON_SHIM: READY` не подключается** — by design. Ставьте
   маркер, только когда реализация рабочая; иначе рантайм можно сломать.
3. **Регистрации обязаны идти до `app.main()`** — иначе статические инстансы
   остаются `null` на момент первого обращения.
4. **Общий плагин — в embedder'е, не в примере.** `plugins_horizon.cpp`
   работает для всех приложений; в `main.cpp` держите только
   приложение-специфичное (`flutter/textinput`, `media_kit_video`).
5. **`raw` vs `method` — по протоколу пакета**, а не по удобству. Ошибка здесь
   даёт «Nachricht nicht dekodierbar» в логе.
6. **`return false` из method-хендлера = `MissingPluginException`.** Не
   используйте как «не умею», если у метода есть корректный ответ (`null`,
   пустой список, `false`).
7. **Всё на платформенном потоке.** Долгие операции (файловый ввод-вывод)
   блокируют рендер — учитывайте это; блокировки не нужны, но и долгие
   операции не бесплатны.
8. **Типизированные массивы не поддержаны** кодером — нужен `Uint8List`,
   сначала расширьте `standard_message_codec.*`.
9. **Честность важнее полноты:** `secure_storage` на FAT-карте — **plaintext**;
   `geolocator` — `denied`; камеры/видео нет. Так и отвечаем.
10. **Язык — русский:** все новые комментарии (C++, Dart, скрипты) и
    документация пишутся по-русски (AGENTS.md §7). Исторические англо/немецкие
    комментарии upstream переводим только вместе с правкой соседнего кода.

---

## 9. Смежное

| Тема | Документ |
|---|---|
| Фундамент: план работ A–I | `docs/infra-plan.md` |
| Ввод: геймпад, стики, виртуальный курсор | `docs/input-icon-plan.md` |
| Сеть/TLS: корни, DoH, «сеть не готова на старте» | `docs/horizon-tls-roots.md` |
| Плагинные планы | `docs/plugins-plan.md`, `docs/plugin-plan-my-nexus.md` |
| Интеграция Flutter-приложения | `docs/flutter-app-integration.md` |
| Архитектура embedder'а | `docs/architecture.md` |
| Сборка на macOS, контракты скриптов | `docs/macos-plan.md`, `docs/macos-migration.md`, `scripts/common.sh` |
| Шаблон плагина + пошаговый чеклист | `examples/plugin_template/README.md` |

---

_Создано в рамках Спринта 3 (G1). При изменении контрактов скриптов/каналов
правьте этот файл и `docs/macos-migration.md` согласованно._
