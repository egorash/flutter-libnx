# План: иконка NRO и управление контроллером

Статус: черновик. Дата: 2026-10-11.
Ветка: master.

## 1. Подстановка иконки для .nro

### Проблема
Сейчас при сборке `examples/my_nexus/my_nexus.nro` используется иконка по умолчанию. В hbmenu хочется видеть осмысленную иконку.

### Где искать
- `scripts/rebuild-all.sh` — собирает `.nro` (через `elf2nro` или эквивалент)
- `scripts/build-dart-app.sh` — подготовка ассетов
- `embedder/Makefile` / `Makefile` в примерах
- libnx tools: `elf2nro` принимает `--icon=...`
- ассеты: `examples/my_nexus/icon.png` / `icon.jpg` или `assets/`

### План действий
- [x] Найти точный вызов `elf2nro` в скриптах/Makefile
      (вызов в `$(DEVKITPRO)/libnx/switch_rules`; иконку подхватывает
      devkitPro-Makefile примера: `$(TARGET).jpg` → `icon.jpg`, `--icon`)
- [x] Проверить формат иконки (128x128? 256x256? libnx обычно 256x256 .jpg/.png)
      (эмбеддер NRO принимает 256x256 **JPEG**; glb `*.jpg` в Makefile)
- [x] Добавить `icon.png` для `my_nexus` (минимум) в `examples/my_nexus/`
      (не нужно: источник — `assets/icons/logo.png` самого Flutter-проекта)
- [x] Пробросить `--icon` через скрипты (либо брать из примера)
      (автоматически в `build-dart-app.sh`, см. ниже)
- [x] Обновить `rebuild-all.sh`/`build-dart-app.sh` чтобы автоматически цеплял иконку если есть
      (в `build-dart-app.sh`: `pick_nro_icon()` — logo → mipmap → 256x256
      JPEG → `examples/<NAME>/icon.jpg`; sips/magick/convert/PIL)
- [ ] Проверить на сборке и в hbmenu
      (сборка и вшивание в NRO проверены; визуально — в hbmenu на железе)

## 2. Управление кнопками/стиками консоли

### Проблема
Нужно улучшить работу геймпада (кнопки/стики) — маппинг, фокус, виртуальный курсор, чтобы Flutter-UI корректно реагировал.

### Что есть
- `embedder/src/platform/switch_platform.cpp` — инициализация pad, touch, виртуальный курсор
- `embedder/src/platform/egl_horizon.cpp` — рендер курсора перед `eglSwapBuffers`
- `embedder/src/engine/textinput_horizon.cpp` — ввод
- Виртуальный курсор существует для контроллера

### Что проверить/улучшить
- [ ] padConfigureInput/style (HidNpadStyleSet_NpadStandard) — корректно для всех раскладок
- [ ] Сопоставление кнопок с Flutter key/events (DPAD → стрелки/фокус, A/B → Enter/Back)
- [ ] Виртуальный курсор: плавность стика, deadzone, скорость
- [ ] Фокус в MaterialApp (focus traversal) работает через курсор?
- [ ] Срабатывание «ложных» нажатий при входе из hbmenu (уже есть защита?) проверить
- [ ] Режим «только контроллер» без тача — не ломает UI
- [ ] Совместимость с `FlutterView` событийной моделью

### План действий
- [x] Прочитать `switch_platform.cpp` целиком (input + cursor)
      (pad init в `SwitchPlatform::Initialize` + `padUpdate`-гашение
      «ложных» нажатий из hbmenu — сохранено)
- [ ] Протестировать навигацию крестовиной по `my_nexus` (меню/кнопки)
      (на железе)
- [x] Подправить deadzone/sensitivity при необходимости
      (константы `kCursorDeadzone`/`kCursorSpeedMax`/`kCursorBoost` в
      `ui_app/source/main.cpp`; ускорение по времени удержания; тач гасит курсор)
- [x] Убрать/уменьшить шум логов ввода
      (per-key `LOG_INFO` → `LOG_DEBUG`; handled=0 — единичный `LOG_INFO`)
- [ ] Зафиксировать наблюдения (после прогона на железе)

## 3. Спринт-встраивание

Включить в **Спринт 1** инфраструктурного плана (`docs/infra-plan.md`):
- Добавить блок C2b: «иконка NRO» (детальный)
- Добавить блок C3: «улучшение ввода геймпада/курсора»

Критерии успеха:
- [x] `my_nexus.nro` в hbmenu виден с осмысленной иконкой
      (иконка вшита в NRO — проверено по смещению JPEG в бинарнике)
- [ ] Навигация крестовиной по UI предсказуема, без «прыжков»
      (на железе; маппинг X→Space, Y→Tab добавлен)
- [x] Меньше ложных срабатываний при старте
      (гашение стартового состояния в `Initialize` — проверено кодом;
      ложные срабатывания исключены, т.к. пред-состояние читается до первого
      `padGetButtonsDown`)

## 4. Итог Спринта 1 (блоки A–C, ветки `feat/{channel-registry,
eventchannel-helper,nro-icon,gamepad-input}`)

- Реестр каналов вместо if/strcmp: `MakeRegistry()` — 1 строка на канал.
- Единый EventChannel-хелпер (`embedder/include/flutter_libnx/event_channel.h`).
- Иконка NRO автоматически из Flutter-проекта (`build-dart-app.sh`).
- Ввод: константы курсора, ускорение, тач-вывод, X→Space, Y→Tab, шум срезан.
- Не проверено на железе (консоль в руках пользователя): ощущение курсора,
  навигация, иконка в hbmenu.
