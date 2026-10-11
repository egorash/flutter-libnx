# План: полноценный `webview_flutter` на Switch (ветка `webview-flutter`)

Статус: черновик плана. Дата: 2026-10-11.
Ветка: `webview-flutter` (от `master` форка).
Контекст: Фаза 0 (итоги в `docs/plugin-plan-my-nexus.md`): `WebViewController()`
в `my_nexus` падает в Dart на `WebViewPlatform.instance!` (47× null-check,
FPS ~5). Решение заказчика: не заглушка, а **полноценный web-view**.

---

## 1. Цель и рамки

Дать приложению реальный встроенный web-view: страницы рендерятся **на
консоли**, API `webview_flutter` 4.9 покрывается максимально, приложение и
эмбеддер не деградируют.

Что физически доступно на Horizon как web-движок:

- **Единственный движок** — системный браузер консоли (NetFront, applet):
  `webPageCreate(&config, url)` + `webConfigShow(&config, nullptr)`. Он
  открывается **на весь экран и перехватывает управление** (главный цикл
  приложения стоит, пока браузер открыт; ответ платформенному каналу
  уходит ДО показа — как в `HandleUrlLauncher`, plugins_horizon.cpp:296).
- Встроить NetFront как inline-виджет в окно Flutter нельзя: апплет всегда
  полноэкранный и не предоставляет поверхность для композитинга.
- Свой движок (Chromium/WebKit) на Switch homebrew нереалистичен
  (сотни МБ, нет системных зависимостей, лицензии).

**Вывод:** «полноценный» здесь = полная реализация интерфейса
`webview_flutter` поверх системного браузера-апплета: навигация по URL —
настоящая (рендерит NetFront), inline-превью — собственный виджет-холст
(состояния, спиннер, ошибки), JS-мосты и DOM-доступ — вне возможностей
(браузер не наш), отдаются честными ошибками.

---

## 2. Протокол webview_flutter 4.9 / platform-interface 2.15.1

Используемые пакеты (pubspec.lock my_nexus):

| Пакет | Версия | Роль |
|---|---|---|
| `webview_flutter` | 4.9.0 | app-facing API (`WebViewController`, `WebViewWidget`, `NavigationDelegate`) |
| `webview_flutter_platform_interface` | 2.15.1 | `WebViewPlatform.instance`, `PlatformWebViewController`, `PlatformWebViewWidget` |

Ключевые точки интеграции (study ◆ сделан по исходникам 2.15.1):

- `WebViewPlatform.instance` — `static set` (webview_platform.dart:31), не null.
  Сейчас null → `WebViewPlatform.instance!` в фабрике
  `PlatformWebViewController` (platform_webview_controller.dart:33) = наш
  null-check.
- `WebViewPlatform` — абстрактный (extends, проверка токена через
  `PlatformInterface.verify`). Переопределяем:
  - `createPlatformWebViewController(params)`;
  - `createPlatformWebViewWidget(params)`;
  - `createPlatformNavigationDelegate(params)`.
- `PlatformWebViewController` — extends; дефолты бросают `UnimplementedError`
  (то есть переопределяем только то, что реально используем):
  - навигация: `loadRequest(LoadRequestParams)`, `loadHtmlString(html)`,
    `loadFileWithParams/loadFile`, `loadFlutterAsset(key)`;
  - состояние: `currentUrl()`, `getTitle()`, `canGoBack/canGoForward()`,
    `goBack/goForward/reload/clearCache/clearLocalStorage/stopLoading`;
  - JS: `runJavaScript`, `runJavaScriptReturningResult`,
    `addJavaScriptChannel/removeJavaScriptChannel`;
  - ввод/настройки: `setPlatformNavigationDelegate`,
    `setJavaScriptMode` (уходит в `CreationParams`), `setBackgroundColor`,
    `scrollTo/scrollBy`, scrollbar-флаги.
- `NavigationDelegate` (app-facing) ↔ `PlatformNavigationDelegate` (ним)
  — колбэки onPageStarted/onPageFinished/onProgress/onWebResourceError/
  onNavigationRequest; наши события генерирует C++-канал или сам шим.
- `WebViewWidget.build` → `platform.build(context)` (webview_widget.dart:120)
  — виджет-холст полностью наш.

---

## 3. Архитектура (два слоя)

```
Dart (app) my_nexus (не трогаем)
   │  WebViewController / WebViewWidget / NavigationDelegate
   ▼
Dart-шим (генерируется build-dart-app.sh, живёт в examples/<app>/generated/,
         приложение не меняется)
   HorizonWebViewPlatform (WebViewPlatform.instance)
   HorizonWebViewController (PlatformWebViewController)
   HorizonWebViewWidget     (холст: состояния/спиннер/ошибка)
   HorizonNavigationDelegate(мост в app-facing NavigationDelegate)
   │  MethodChannel 'flutter-libnx/webview'
   ▼
C++ (embedder, plugins_horizon.cpp / новый webview_horizon.cpp)
   launch(url) / close() / status → NetFront applet (webPageCreate+
   webConfigShow), file://-проводник для загруженного HTML
```

Почему не Pigeon-каналы плагина: `webview_flutter_platform_interface` на
не-Android/iOS платформах не имеет канальной реализации — Dart падает на
фабрике раньше любых каналов. Поэтому реализацию несёт Dart-шим, а канал
`flutter-libnx/webview` — собственный (StandardMethodCodec), по образцу
существующих хендлеров.

---

## 4. Этапы (чеклист)

### Этап 1 — каркас: шим + C++-канал
- [ ] C++: канал `flutter-libnx/webview` (StandardMethodCodec):
  - [ ] `launch {url}` → `webPageCreate`+`webConfigShow` (переиспользование
        кода url_launcher), ответ true до показа;
  - [ ] `close` → quittieren (браузер закрыт самим пользователем);
  - [ ] `status` → «был ли хоть раз открыт» (для состояния виджета);
  - [ ] путь для временных файлов: `/switch/flutter_apps/<id>/webview/`.
- [ ] build-dart-app.sh: для `webview_flutter` в package_config → генерировать
      `horizon_main.dart` с `WebViewPlatform.instance = HorizonWebViewPlatform()`
      (по образцу `FilePickerIO.registerWith()`).
- [ ] Dart-шим: `HorizonWebViewPlatform` + `HorizonWebViewController`
      (минимально: loadRequest → launch; остальное — честные ошибки/состояния),
      `HorizonWebViewWidget` (пустой холст + спиннер).
- [ ] Проверка: заметка с треком открывается, null-check спама нет.

### Этап 2 — навигация по URL (треки, внешние ссылки)
- [ ] `loadRequest(LoadRequestParams{url, method, headers, body})` → launch
      (http/https; иные схемы — честная ошибка `PlatformException`).
- [ ] Состояние: `HorizonWebViewWidget` показывает спиннер на время
      «навигация начата → браузер открыт»; `onPageStarted/onPageFinished`
      уходят в app-facing `NavigationDelegate` (спиннер в TrackSection
      гаснет); `currentUrl()`/`getTitle()` возвращают последний URL.
- [ ] `canGoBack/canGoForward` → false (браузер держит свою историю),
      `reload/stopLoading` → no-op/честно.
- [ ] Приёмка: треки в играх/фильмах/книгах/путешествиях открываются в
      браузере консоли; по возврату — приложение живо, спиннера нет.

### Этап 3 — loadHtmlString / loadFile / loadFlutterAsset
- [ ] `loadHtmlString(html)` и `loadFile(path)` → сохраняем во временный
      файл `/switch/flutter_apps/<id>/webview/<hash>.html` (Dart-или-C++-файл
      IO — решить; скорее C++: атомарный write как в shared_preferences).
- [ ] Проверка на железе: открывает ли NetFront `file://` (URL-трансформация
      `file:///switch/...`). Если да — музыкальные заметки (iframe-коды)
      рендерятся по-настоящему.
- [ ] Если NetFront не открывает file:// — fallback: честная ошибка
      `PlatformException('unsupported_scheme')`, блок пуст с подписью.
- [ ] `loadFlutterAsset(key)` — ассеты недоступны браузеру → честная ошибка.

### Этап 4 — JS-мосты
- [ ] `runJavaScript`/`runJavaScriptReturningResult`, `addJavaScriptChannel` →
      `PlatformException('no_js_bridge')`: у NetFront нет внешнего JS-моста.
      (Если в будущем появится способ — расширить.)

### Этап 5 — декларации и настройки
- [ ] `setJavaScriptMode`/`setBackgroundColor`/`userAgent` — принимаются,
      влияния нет (браузер собственный);
- [ ] cookie manager/clearCache/скролл — честные «недоступно» без броска
      в build (возврат значений по умолчанию);
- [ ] `WebViewWidget`-холст довести: фон из темы (backgroundColor из
      CreationParams), отсутствие рамок, корректный размер.

### Этап 6 — приёмка на железе (полный прогон)
- [ ] Сборка NRO, заливка, прогон сценариев Фазы 0 (карта — не ломает;
      фото — работает; музыкальная заметка — iframe реально открывается
      ИЛИ пусто с подписью; треки — браузер);
- [ ] Нет «Another exception» спама, FPS стабильные;
- [ ] Документация: `docs/macos-migration.md`, README, итоги в
      `docs/plugin-plan-my-nexus.md`.

---

## 5. Открытые вопросы (решения нужны до Этапа 2)

1. **Fullscreen vs inline.** Системный браузер всегда полноэкранный и
   блокирует приложение до закрытия. Treки открываются так — ок?
   (Альтернативы нет: это единственный web-движок.)
2. **Музыкальные iframe.** Реальный рендер возможен только если NetFront
   открывает file://  (Этап 3, проверка на железе). Если нет — iframe
   остаётся честной пустотой с подписью «Плеер недоступен» (выбор
   пользователя: «Пусто»).
3. **JS-зависимые страницы** (YouTube-embed-плеер, SPAs) — могут работать
   частично или не работать в NetFront; критерий приёмки — «не крашит» +
   «контент виден там, где NetFront может».

## 6. Риски

- NetFront не открывает file:// → еслиrame-заметки не рендерятся
  (приемлемый fallback готов).
- Полноэкранность браузера меняет UX треков (выход в браузер, возврат
  кнопкой Home/плюс) — задокументировать.
- Объём Dart-шима (~300–400 строк) — контролируется обзорами на этапах.

## 7. Критерии приёмки

- `WebViewController()` в my_nexus не бросает, null-check-спама нет;
- треки (URL) открываются системным браузером и корректно возвращают
  управление; спиннеры гаснут;
- музыкальная заметка не роняет приложение; iframe либо рендерится
  (file://), либо честно помечен «Плеер недоступен»;
- API-методы, которые физически невозможны, отвечают честными ошибками,
  ни один не падает в build.