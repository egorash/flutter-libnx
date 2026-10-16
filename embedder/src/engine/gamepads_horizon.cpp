// Канал плагина gamepads (xyz.luan/gamepads).
//
// Протокол разобран по исходникам пакета
// (gamepads_platform_interface-0.1.4/lib/method_channel_gamepads_platform_interface.dart):
//
//   MethodChannel  xyz.luan/gamepads
//     Dart -> Plattform:  listGamepads, hasRumble, rumble, stopRumble
//     Plattform -> Dart:  onGamepadEvent, onGamepadConnectionEvent
//
// Оба направления идут по ОДНОМУ каналу, поэтому исходящие события -
// обычные вызовы методов, на которые Dart не отвечает.
//
// Смысл числовых ключей и инверсия оси Y объяснены в gamepad_horizon.h.
#include <chrono>
#include <string>
#include <vector>

#include "embedder.h"
#include "flutter_libnx/gamepad_horizon.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {
namespace {

using flutter_libnx::StdValue;

void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
             const FlutterPlatformMessage* message,
             const StdValue& result) {
  if (message->response_handle == nullptr) {
    return;
  }
  const std::vector<uint8_t> payload = EncodeStdSuccess(result);
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

const StdValue* Find(const StdValue& args, const std::string& key) {
  for (const auto& entry : args.as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

double Number(const StdValue& args, const std::string& key, double fallback) {
  const StdValue* value = Find(args, key);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type == StdValue::Type::kDouble) {
    return value->as_double;
  }
  if (value->type == StdValue::Type::kInt) {
    return static_cast<double>(value->as_int);
  }
  return fallback;
}

std::string Text(const StdValue& args, const std::string& key) {
  const StdValue* value = Find(args, key);
  if (value == nullptr || value->type != StdValue::Type::kString) {
    return std::string();
  }
  return value->as_string;
}

// Отправляет вызов метода в Dart без ответа (Dart на него и не ждёт:
// методы onGamepadEvent и onGamepadConnectionEvent ответа не имеют).
void Invoke(FLUTTER_API_SYMBOL(FlutterEngine) engine,
            const std::string& method,
            const StdValue& args) {
  const std::vector<uint8_t> payload = EncodeStdMethodCall(method, args);
  FlutterPlatformMessage message = {};
  message.struct_size = sizeof(message);
  message.channel = "xyz.luan/gamepads";
  message.message = payload.data();
  message.message_size = payload.size();
  // Kein response_handle: reiner Aufruf ohne Antwort.
  FlutterEngineSendPlatformMessage(engine, &message);
}

int64_t NowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace
}  // namespace flutter_libnx

extern "C" {

bool flutter_libnx_gamepads_handle(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message,
    const std::string& method,
    const flutter_libnx::StdValue& args) {
  using namespace flutter_libnx;

  if (method == "listGamepads") {
    const std::string id = flutter_libnx_gamepad_id();
    LOG_INFO("gamepads: listGamepads -> %s",
             id.empty() ? "kein Controller" : id.c_str());
    if (id.empty()) {
      // Пустой список - честный ответ: контроллера нет.
      Respond(engine, message, StdValue::List());
      return true;
    }
    StdValue controller = StdValue::Map();
    controller.as_map.emplace_back(StdValue::String("id"), StdValue::String(id));
    controller.as_map.emplace_back(StdValue::String("name"),
                                   StdValue::String(flutter_libnx_gamepad_name()));
    // vendorId/productId не шлём: LinuxMapping без них подставляет
    // ControllerDatabase.defaultMapping (linux_mapping.dart:44), а это и есть
    // та раскладка, под которую слались числовые ключи.
    controller.as_map.emplace_back(StdValue::String("vendorId"), StdValue::Null());
    controller.as_map.emplace_back(StdValue::String("productId"), StdValue::Null());

    StdValue list = StdValue::List();
    list.as_list.push_back(controller);
    Respond(engine, message, list);
    return true;
  }

  if (method == "hasRumble") {
    Respond(engine, message, StdValue::Bool(flutter_libnx_gamepad_has_rumble()));
    return true;
  }

  if (method == "rumble") {
    const bool ok = flutter_libnx_gamepad_rumble(
        static_cast<float>(Number(args, "lowFrequency", 0.0)),
        static_cast<float>(Number(args, "highFrequency", 0.0)),
        static_cast<int>(Number(args, "durationMillis", 500)));
    Respond(engine, message, StdValue::Bool(ok));
    return true;
  }

  if (method == "stopRumble") {
    flutter_libnx_gamepad_stop_rumble();
    Respond(engine, message, StdValue::Bool(true));
    return true;
  }

  // Методы, которыми Dart шлёт нам события. Нативная часть их не инициирует,
  // но отвечать надо - иначе Dart ждёт вечно.
  if (method == "onGamepadEvent" || method == "onGamepadConnectionEvent") {
    LOG_WARN("gamepads: unerwarteter Aufruf %s von Dart", method.c_str());
    Respond(engine, message, StdValue::Null());
    return true;
  }

  Respond(engine, message, StdValue::Null());
  return true;
}

void flutter_libnx_gamepads_pump(FLUTTER_API_SYMBOL(FlutterEngine) engine) {
  using namespace flutter_libnx;

  // Первые события пишем подробно, дальше - счётчиком раз в пять секунд.
  // Подробный лог на каждое движение оси забил бы файл и ничего бы не
  // добавил: событий там единицы на нажатие.
  static int g_logged = 0;
  static int64_t g_summary_at = 0;
  static int g_buttons = 0;
  static int g_axes = 0;

  flutter_libnx::PadEvent event;
  while (flutter_libnx_gamepad_pop_event(&event)) {
    const std::string id = flutter_libnx_gamepad_id();
    if (id.empty()) {
      continue;
    }

    StdValue payload = StdValue::Map();
    payload.as_map.emplace_back(StdValue::String("gamepadId"),
                                StdValue::String(id));

    if (event.connection) {
      payload.as_map.emplace_back(StdValue::String("name"),
                                  StdValue::String(flutter_libnx_gamepad_name()));
      payload.as_map.emplace_back(
          StdValue::String("type"),
          StdValue::String(event.connected ? "connected" : "disconnected"));
      LOG_INFO("gamepads: %s",
               event.connected ? "Controller verbunden" : "Controller getrennt");
      Invoke(engine, "onGamepadConnectionEvent", payload);
      continue;
    }

    if (event.analog) {
      g_axes++;
    } else {
      g_buttons++;
    }
    if (g_logged < 40) {
      g_logged++;
      LOG_INFO("gamepads: %s key=%s value=%.0f",
               event.analog ? "Achse" : "Taste", event.key.c_str(), event.value);
    }

    payload.as_map.emplace_back(StdValue::String("time"),
                                StdValue::Int(NowMillis()));
    payload.as_map.emplace_back(
        StdValue::String("type"),
        StdValue::String(event.analog ? "analog" : "button"));
    payload.as_map.emplace_back(StdValue::String("key"),
                                StdValue::String(event.key));
    payload.as_map.emplace_back(StdValue::String("value"),
                                StdValue::Double(event.value));
    payload.as_map.emplace_back(StdValue::String("vendorId"), StdValue::Null());
    payload.as_map.emplace_back(StdValue::String("productId"), StdValue::Null());
    Invoke(engine, "onGamepadEvent", payload);
  }

  const int64_t now = NowMillis();
  if (g_buttons + g_axes > 0 && now >= g_summary_at) {
    g_summary_at = now + 5000;
    LOG_INFO("gamepads: bisher %d Tasten und %d Achsenbewegungen gesendet",
             g_buttons, g_axes);
  }
}

}  // extern "C"