// Геймпад через libnx pad и сервис-плагин gamepads.
//
// Почему ключи числовые, а не "KEYCODE_BUTTON_A": приложение нормализует
// события через GamepadNormalizer, который выбирает таблицу по
// defaultTargetPlatform (gamepads-0.1.12/lib/src/gamepad_normalizer.dart:78).
// На Switch эта платформа - linux, потому что Platform.operatingSystem у нас
// возвращает "linux" (наш патч в dart/runtime/bin/platform.h:37, иначе
// Flutter падает на FocusManager). Значит игра берёт LinuxMapping и ждёт
// числовые SDL-ключи из ControllerDatabase.defaultMapping.
//
// Две особенности, из-за которых маппинг и не получался "как ожидалось":
//
//  * Ось Y инвертирована. В defaultMapping стоит yAxisInverted = true, то есть
//    нормализатор сам меняет знак. Значит слать надо по конвенции SDL:
//    вверх - отрицательное значение. У libnx наоборот, вверх - положительное,
//    поэтому Y отправляем с минусом.
//  * D-Pad у libnx это кнопки, а LinuxMapping ждёт ось-шляпу: из кнопок
//    синтезируется ось "6" (X) и "7" (Y). Из них нормализатор сам
//    получается GamepadButton.dpadLeft/Right/Up/Down.
//
// Отправляются только изменения: без этого поток событий упирается в лимит
// BSD-сессий, а лог засоряется.
#pragma once

#include <string>

#include "embedder.h"

namespace flutter_libnx {

// Один ввод для отправки в Dart. Соединения и обычные события разведены
// флагом connection: у них разные протоколы (onGamepadConnectionEvent против
// onGamepadEvent).
struct PadEvent {
  bool connection = false;
  bool connected = false;   // только для connection
  bool analog = false;      // false = кнопка, true = ось
  std::string key;          // "0".."10" для кнопок, "0".."7" для осей
  double value = 0.0;
};

}  // namespace flutter_libnx

extern "C" {

// Поднимает опрос геймпада в отдельном потоке. false с текстом ошибки, если
// libnx не даёт контроллер.
bool flutter_libnx_gamepad_init(std::string* error);

void flutter_libnx_gamepad_shutdown(void);

// Идентификатор подключённого геймпада для listGamepads; пусто, если
// контроллера нет.
std::string flutter_libnx_gamepad_id(void);

// Имя геймпада для listGamepads.
std::string flutter_libnx_gamepad_name(void);

// Вибрация: true, если устройство её умеет (Pro Controller - да).
bool flutter_libnx_gamepad_has_rumble(void);
bool flutter_libnx_gamepad_rumble(float low, float high, int duration_ms);
void flutter_libnx_gamepad_stop_rumble(void);

// Забирает накопленное событие. Вызывать из главного цикла на
// платформенном потоке: отправлять в Dart можно только оттуда.
bool flutter_libnx_gamepad_pop_event(flutter_libnx::PadEvent* event);

}  // extern "C"