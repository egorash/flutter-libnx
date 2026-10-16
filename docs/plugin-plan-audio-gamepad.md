# План: звук (`audioplayers`) и геймпад (`gamepads`) для romashka_game

Планирование, реализация не начата. Язык — русский.

Приоритет задан пользователем: **звук обязателен**, геймпад желателен.

---

## 1. Что уже есть в порту

Плагины в порту не «регистрируются» — эмбеддер отвечает на каналы. Реестр в
`embedder/src/engine/plugins_horizon.cpp:855` (`MakeRegistry`):

- `r.method` — `StandardMethodCodec`: метод + аргументы, ответ через `EncodeStd*`;
- `r.raw` — каналы со своим протоколом (декодируют сообщение сами);
- неизвестный канал → `LogUnknownChannelOnce` пишет предупреждение в лог и
  приложение получает `MissingPluginException` (`plugins_horizon.cpp:878`).

Уже реализованы: `path_provider`, `shared_preferences`, `url_launcher`,
`image_picker`, `secure_storage`, `geolocator`, `file_picker`.
Для потоковых каналов есть готовый хелпер
`embedder/include/flutter_libnx/event_channel.h` + `event_channel_horizon.cpp`
(на нём построен geolocator) — переиспользовать, не писать заново.

**Побочная поправка к прежним notes:** `shared_preferences` в игре работать
будет. `SharedPreferences.getInstance()` при отсутствии зарегистрированного
плагина садится на старый канал `plugins.flutter.io/shared_preferences`
(`shared_preferences_platform_interface-2.4.1/lib/shared_preferences_platform_interface.dart:41`),
а он в порту есть. Сохранение прогресса не потеряно.

---

## 2. Звук: `audioplayers`

### 2.1 Протокол (прочитан в пакете, не додуман)

- MethodChannel `xyz.luan/audioplayers` (`audioplayers_platform_interface-7.1.1/lib/src/audioplayers_platform.dart:32`).
  Методы: `create`, `dispose`, `release`, `play`/`pause`/`resume`/`stop`, `seek`,
  `setVolume`, `setBalance`, `setPlaybackRate`, `setReleaseMode`, `setPlayerMode`,
  `setAudioContext`, `setSourceUrl`, `setSourceBytes`, `getCurrentPosition`,
  `getDuration`, `emitLog`, `emitError`. В аргументах всегда `playerId`.
- EventChannel **на каждый игрок**: `xyz.luan/audioplayers/events/<playerId>`
  (`audioplayers_platform.dart:242`). События — словари
  `{"event": "audio.onPrepared", "value": true}`, `audio.onDuration`,
  `audio.onComplete`, `audio.onSeekComplete`, `audio.onLog`.
- MethodChannel `xyz.luan/audioplayers.global` и EventChannel
  `xyz.luan/audioplayers.global/events` (`global_audioplayers_platform.dart:18`).

### 2.2 Условие, без которого звук не «заработает», а «зависнет»

`AudioPlayer.play()` → `_completePrepared()` ждёт события `onPrepared`
(`audioplayer.dart:358-372`). Таймаут — **30 секунд**
(`audioplayer.dart:22`). Если EventChannel не отвечает, каждый вызов `play()`
висит полминуты. Значит событие `audio.onPrepared` с `value: true` —
обязательная часть контракта, а не украшение.

### 2.3 Откуда берутся байты

Для `AssetSource` Dart-сторона сама читает ассет через `rootBundle` и
**записывает во временный файл**: `${getTempDir()}/$cacheId/$fileName`
(`audio_cache.dart:100-106`). `getTempDir()` — это `path_provider`, а он в
порту реализован: `/switch/flutter_apps/romashka_game/tmp`
(`plugins_horizon.cpp:92`). Дальше приходит `setSourceUrl(..., isLocal: true)`
с этим путём.

Отсюда хорошая новость: **нативной части достаточно читать обычный файл с
SD-карты** через существующий слой `posix_compat`. RomFS-обход не нужен.

### 2.4 Нагрузка — отсюда архитектура

В игре 15 коротких звуков в пулах (`audio_service.dart:239-253`), сумма
размеров пулов — **30**, плюс `FlameAudio.bgm` — итого до **31 одновременного
игрока**. Все 16 файлов — MP3 (`assets/audio/*.mp3`), 3.7 МБ.

Вывод: «декодер на игрока» не годится — 31 поток декодирования на
четырёхядерной консоли, где порт уже наступал на границы числа потоков.
Нужен **один аудио-поток и микшер**:

- голос = состояние minimp3 + кольцевой буфер PCM;
- поток ждёт кадры `audrenWaitFrame()`, для каждого активного голоса
  добирает декодированные кадры, суммирует, отдаёт в `audren`;
- MethodChannel-обработчики только управляют списком голосов с
  платформенного потока (данные защищены короткой блокировкой).

Число потоков: **+1** (аудио), плюс **+1** на геймпад (см. §3).

### 2.5 Декодер: развилка

| | minimp3 (вендорим) | FFmpeg (уже линкуется) |
|---|---|---|
| Объём | один заголовок ~80 КБ | уже в NRO ради media_kit |
| Инициализация | нет | поиск декодера, контекст |
| Память | минимальная на голос | заметная на голос |
| Форматы | только MP3 | всё |
| Риск | вендорим чужой код (~1000 строк) | тяжёлый API, конфликты символов с media_kit |

Рекомендация: **minimp3** — ассеты игры на 100% MP3, а независимость от
media_kit того стоит. FFmpeg оставить запасным вариантом, если появятся
ogg/wav.

**Решение (2026-10-16): minimp3.** Ассеты игры — 16 файлов MP3, вокапом до 31
источника, поэтому вес одного голоса решает. FFmpeg остаётся запасным
вариантом на случай появления ogg/wav.

### 2.6 Блоки работ

- [ ] **A1.** `audren`: инициализация 48 кГц / стерео / 16 бит, отдельный поток,
      кадр за кадром, корректный выход. Проверка: тишина без ошибок в логе.
- [ ] **A2.** Голос: minimp3 + кольцевой буфер, 31 голос, микширование с
      ограничением по громкости.
- [ ] **A3.** MethodChannel `xyz.luan/audioplayers`: полный набор методов;
      `setSourceUrl` открывает файл через `posix_compat`.
- [ ] **A4.** EventChannel на игрок: `onPrepared` (обязательно), `onDuration`,
      `onComplete`, `onSeekComplete`. Имена каналов динамические — в реестре
      нужен **префиксный** поиск, а не точное совпадение.
- [ ] **A5.** `xyz.luan/audioplayers.global` — заглушки (resume/stop контекста).

### 2.7 Риски

- 30 с таймаута на каждый `play()`, если событие не уходит — самая дорогая
  ошибка; лечится проверкой в A4.
- Переполнение кольцевого буфера при перегрузке (много одновременных
  источников) — нужен аккуратный отсчёт «сколько декодировать».
- `audren` требует аккуратного порядка `RequestUpdateAudioRenderer` /
  `StartAudioRenderer`; ошибка здесь — тишина без внятного сообщения.
- Потоки: +1 аудио, что при уже плотной картине потоков порта требует
  проверки на железе.

---

## 3. Геймпад: `gamepads`

### 3.1 Ключевой факт, который объясняет «не тот маппинг»

`GamepadNormalizer` выбирает таблицу по `defaultTargetPlatform`
(`gamepads-0.1.12/lib/src/gamepad_normalizer.dart:78`). А `defaultTargetPlatform`
на Switch равен **linux**, потому что `Platform.operatingSystem` у нас
возвращает `"linux"` — это сделано нашим же патчем
(`dart/runtime/bin/platform.h:37`), иначе Flutter не знает платформу и
бросает исключение при создании `FocusManager`.

Значит игра возьмёт **LinuxMapping**, и нативная часть обязана слать ключи
**в SDL-стиле, числами** — не `KEYCODE_BUTTON_A`, не `a`.

### 3.2 Таблица соответствия

Кнопки (`gamepads_controller_database.dart:141`, порядок Xbox):

| libnx | ключ | смысл |
|---|---|---|
| `HidNpadButton_A` | `"0"` | A |
| `HidNpadButton_B` | `"1"` | B |
| `HidNpadButton_X` | `"2"` | X |
| `HidNpadButton_Y` | `"3"` | Y |
| `HidNpadButton_L` | `"4"` | LB |
| `HidNpadButton_R` | `"5"` | RB |
| `HidNpadButton_Minus` | `"6"` | Back |
| `HidNpadButton_Plus` | `"7"` | Start |
| `HidNpadButton_Screenshot` | `"8"` | Home |
| `HidNpadButton_StickL` | `"9"` | L3 |
| `HidNpadButton_StickR` | `"10"` | R3 |

Оси (`controller_database.dart:154`):

| libnx | ключ | диапазон | примечание |
|---|---|---|---|
| `sticks[0].x` | `"0"` | −32768..32767 | левый стик X |
| `sticks[0].y` | `"1"` | −32768..32767 | левый стик Y, **инверсия** |
| ZL | `"2"` | — | как триггер |
| `sticks[1].x` | `"3"` | −32768..32767 | правый стик X |
| `sticks[1].y` | `"4"` | −32768..32767 | правый стик Y |
| ZR | `"5"` | — | как триггер |
| D-Pad лево/право | `"6"` | ±32767 | `dpadAxes['6'] = true` |
| D-Pad вверх/вниз | `"7"` | −32767 = вверх | `yAxisInverted = true` |

Значения `sticks` у libnx — `s32` в диапазоне ±32768, то есть масштабировать
не нужно. **D-Pad у libnx — кнопки**, а LinuxMapping ждёт ось-шляпу: значит
нативная часть синтезирует ось `"6"`/`"7"` (0 — отпущено, ±32767 — нажато).

`vendorId`/`productId` можно не слать: `LinuxMapping` при `bestEffort`
подставляет `ControllerDatabase.defaultMapping` (`linux_mapping.dart:44-47`).

### 3.3 Протокол

- MethodChannel `xyz.luan/gamepads`
  (`gamepads_platform_interface-0.1.4/lib/method_channel_gamepads_platform_interface.dart:9`):
  `listGamepads` → список `{id, name, vendorId, productId}`;
  `hasRumble`, `rumble`, `stopRumble`.
- От платформы в Dart на том же канале: `onGamepadEvent`
  (`{gamepadId, time, type: 'button'|'analog', key, value, vendorId, productId}`,
  `gamepad_event.dart:58`) и `onGamepadConnectionEvent`
  (`{gamepadId, name, type}`, `gamepad_connection_event.dart:36`).

### 3.4 Блоки работ

- [ ] **G1.** `padInitializeDefault` + поток опроса ~60 Гц, чтение `padGetState`.
- [ ] **G2.** Диффы состояния → события только при изменении (иначе поток
      событий упирается в 16 BSD-сессий и в лог).
- [ ] **G3.** Отправка `onGamepadEvent` по таблице §3.2, включая синтез осей
      D-Pad.
- [ ] **G4.** `listGamepads` при старте; `onGamepadConnectionEvent` при
      подключении/отключении.
- [ ] **G5.** Вибрация — **в объёме спринта** (решение 2026-10-16):
      `hidInitializeVibrationDevices` + `hidSendVibrationValue`
      (`hid.h:2071`, `2086`), плюс `hasRumble`/`rumble`/`stopRumble` на
      MethodChannel. Для Pro Controller `hasRumble` отвечает `true`.

### 3.5 Риски

- Переполнение канала событий: без диффов (G2) — лавина сообщений.
- Игры без D-Pad как оси: наш синтез покрывает, но на будущее лучше слать
  и кнопки D-Pad как кнопки — но LinuxMapping их не примет, так что решение
  зафиксировано синтезом осей.
- Поток опроса на 60 Гц плюс аудио — проверить суммарное число потоков.

---

## 4. Что не входит

- Правка исходников игры (`audio_service.dart`, `input_controller.dart`) —
  работаем на стороне порта.
- `flame_audio`-специфичные оптимизации (свой пул, стриминг) — не трогаем.
- Поддержка форматов звука кроме MP3.
- Полноценная калибровка осей (мёртвая зона, кривые отклика).

---

## 5. Решения и открытые вопросы

**Решено (2026-10-16):**

1. Звук обязателен, геймпад желателен — приоритет задан.
2. Декодер — minimp3 (см. §2.5).
3. Вибрация входит в объём спринта (см. G5).

**Открытый вопрос:**

Можно ли проверять на железе в debug-режиме? Сейчас debug-`.nro` не
линкуется — не хватает четырёх портов (`docs/build-modes.md` §6), а в
release логи `developer.log` не видны. Для отладки микшера и раскладки
кнопок логи были бы очень кстати: без них не видно, что происходит на
`audren` и какие события уходят в Dart.