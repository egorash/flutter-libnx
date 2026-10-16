// Опрос геймпада. Раскладка и обоснование - в
// include/flutter_libnx/gamepad_horizon.h, здесь реализация.
//
// Раскладка:
//
//   * Один поток читает padGetState и раскладывает изменения в очередь.
//     Наружу (методам канала) он не выходит, кроме вибрации.
//   * Состояние прошлое хранится здесь же, поэтому наружу уходят только
//     изменившиеся значения: без этого поток событий ловит предел BSD-сессий,
//     а лог растёт на каждый кадр опроса.
//   * D-Pad синтезируется осью из кнопок - LinuxMapping ждёт именно шляпу,
//     а libnx её не отдаёт.

#include "flutter_libnx/gamepad_horizon.h"

#include <switch.h>

#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flutter_libnx/log.h"

namespace flutter_libnx {
namespace {

constexpr int kPollHz = 60;
constexpr int kStickMax = 32767;
// Ось-шляпа для D-Pad: крайние значения плюс ноль в покое.
constexpr int kDpadMax = 32767;
constexpr int64_t kPollPeriodNanos = 1000000000 / kPollHz;

// Таблица кнопок libnx -> ключ LinuxMapping (controller_database.dart:141).
// Порядок Xbox, поэтому A=0, B=1, X=2, Y=3.
//
// Ключа "8" (Home) в таблице нет намеренно: у libnx нет константы для
// системной HOME-кнопки, npad её не отдаёт. Игра на неё не завязана.
struct ButtonEntry {
  u64 mask;
  const char* key;
};

constexpr ButtonEntry kButtons[] = {
    {HidNpadButton_A, "0"},      {HidNpadButton_B, "1"},
    {HidNpadButton_X, "2"},      {HidNpadButton_Y, "3"},
    {HidNpadButton_L, "4"},      {HidNpadButton_R, "5"},
    {HidNpadButton_Minus, "6"},  {HidNpadButton_Plus, "7"},
    {HidNpadButton_StickL, "9"}, {HidNpadButton_StickR, "10"},
};

// Прошлое состояние по ключу: 0 - было, 1 - было нажато/ненулевое.
std::string& LastButton(const std::string& key) {
  static std::vector<std::pair<std::string, double>> state;
  for (auto& entry : state) {
    if (entry.first == key) {
      return entry.first;
    }
  }
  state.emplace_back(key, 0.0);
  return state.back().first;
}

double LastValue(const std::string& key) {
  static std::vector<std::pair<std::string, double>> state;
  for (auto& entry : state) {
    if (entry.first == key) {
      return entry.second;
    }
  }
  state.emplace_back(key, 0.0);
  return state.back().second;
}

void Remember(const std::string& key, double value) {
  static std::vector<std::pair<std::string, double>> state;
  for (auto& entry : state) {
    if (entry.first == key) {
      entry.second = value;
      return;
    }
  }
  state.emplace_back(key, value);
}

// Порог, ниже которого ось считается покоящейся: иначе дребезг аналогового
// стика даёт поток событий при простом отпускании.
constexpr double kAxisEpsilon = 3000.0;

class Gamepad {
 public:
  static Gamepad& Instance() {
    static Gamepad instance;
    return instance;
  }

  bool Initialize(std::string* error) {
    if (running_.load()) {
      return true;
    }
    padInitializeDefault(&pad_);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    // Идентификатор для listGamepads. Плагин считает его непрозрачной строкой
    // (GamepadController.parse читает id как String), поэтому подойдёт любая
    // устойчивая. Раньше это поле просто не заполнялось - и listGamepads
    // всегда отвечал "kein Controller", а откачка событий всё отбрасывала.
    id_ = "horizon-pad-" + std::to_string(static_cast<int>(npad_id_));

    // Вибрация: отдельные дескрипторы устройств. Стиль - FullKey, это
    // Pro Controller (обычная пара Джойконов в доке).
    Result res = hidInitializeVibrationDevices(handles_, kVibrationHandles,
                                              npad_id_,
                                              HidNpadStyleTag_NpadFullKey);
    if (R_FAILED(res)) {
      // Не ошибка: без FRD-вибрации игра просто не трясётся.
      LOG_WARN("Gamepad: Vibration nicht verfuegbar (%d)", static_cast<int>(res));
      rumble_ok_ = false;
    } else {
      rumble_ok_ = true;
    }

    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { PollLoop(); });
    LOG_INFO("Gamepad: Abfrage laeuft, 60 Hz, Rumble=%s",
             rumble_ok_ ? "ja" : "nein");
    return true;
  }

  void Shutdown() {
    if (!running_.exchange(false)) {
      return;
    }
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    if (rumble_ok_) {
      for (HidVibrationDeviceHandle handle : handles_) {
        if (handle.type_value != 0) {
          hidSendVibrationValue(handle, &kStill_);
        }
      }
    }
  }

  std::string Id() const { return id_; }

  std::string Name() const { return "Nintendo Switch Controller"; }

  bool HasRumble() const { return rumble_ok_; }

  bool Rumble(float low, float high, int duration_ms) {
    if (!rumble_ok_) {
      return false;
    }
    HidVibrationValue value = {};
    value.amp_low = low;
    value.freq_low = 180.0f;
    value.amp_high = high;
    value.freq_high = 125.0f;
    for (HidVibrationDeviceHandle handle : handles_) {
      if (handle.type_value != 0) {
        Result res = hidSendVibrationValue(handle, &value);
        if (R_FAILED(res)) {
          return false;
        }
      }
    }
    // У libnx нет отдельного "выключить через миллисекунды": значение само
    // спадает. Длительность держим здесь, чтобы вернуть ноль вовремя.
    StopRumbleAfter(duration_ms);
    return true;
  }

  void StopRumble() {
    for (HidVibrationDeviceHandle handle : handles_) {
      if (handle.type_value != 0) {
        hidSendVibrationValue(handle, &kStill_);
      }
    }
    std::lock_guard<std::mutex> guard(mutex_);
    rumble_until_ns_ = 0;
  }

  bool PopEvent(PadEvent* event) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (events_.empty()) {
      return false;
    }
    *event = events_.front();
    events_.pop_front();
    return true;
  }

 private:
  static constexpr int kVibrationHandles = 2;
  static constexpr HidVibrationValue kStill_ = {};

  void StopRumbleAfter(int duration_ms) {
    std::lock_guard<std::mutex> guard(mutex_);
    rumble_until_ns_ = duration_ms > 0 ? duration_ms * 1000000LL : 0;
  }

  void Push(const PadEvent& event) {
    std::lock_guard<std::mutex> guard(mutex_);
    events_.push_back(event);
  }

  void PollLoop() {
    bool announced = false;
    u64 next_poll = 0;

    while (!stop_.load()) {
      // armGetSystemTick zaehlt in Nanosekunden seit Start - direkt als
      // Zeitbasis brauchbar, ohne Umrechnung.
      const u64 now = armGetSystemTick();
      if (now >= next_poll) {
        next_poll = now + kPollPeriodNanos;
        padUpdate(&pad_);

        const u64 active = pad_.active_id_mask & 0xFF;
        if (active != 0) {
          if (!announced) {
            announced = true;
            PadEvent event;
            event.connection = true;
            event.connected = true;
            Push(event);
            LOG_INFO("Gamepad: Controller erkannt");
          }
          PollState();
        } else if (announced) {
          announced = false;
          PadEvent event;
          event.connection = true;
          event.connected = false;
          Push(event);
          LOG_INFO("Gamepad: Controller verschwunden");
        }

        if (rumble_until_ns_ > 0 && now >= rumble_until_ns_) {
          StopRumble();
        }
      }
      svcSleepThread(1'000'000);  // 1 ms
    }
  }

  // Раскладывает состояние в события, отправляя только изменения.
  void PollState() {
    const u64 buttons = pad_.buttons_cur;
    for (const ButtonEntry& entry : kButtons) {
      const double value = (buttons & entry.mask) ? 1.0 : 0.0;
      if (std::fabs(LastValue(entry.key) - value) > 0.0) {
        Remember(entry.key, value);
        PadEvent event;
        event.analog = false;
        event.key = entry.key;
        event.value = value;
        Push(event);
      }
    }

    const HidAnalogStickState& left = pad_.sticks[0];
    const HidAnalogStickState& right = pad_.sticks[1];

    SendAxis("0", left.x);
    // Минус: у libns вверх положительно, у LinuxMapping вверх отрицательно
    // (см. заголовок файла).
    SendAxis("1", -left.y);
    SendAxis("3", right.x);
    SendAxis("4", -right.y);

    // Триггеры у Pro Controller цифровые, но LinuxMapping ждёт ось с
    // диапазоном -32768..32767 - отдаём края диапазона.
    SendAxis("2", (buttons & HidNpadButton_ZL) ? kStickMax : -kStickMax);
    SendAxis("5", (buttons & HidNpadButton_ZR) ? kStickMax : -kStickMax);

    SendAxis("6", DpadAxisX(buttons));
    SendAxis("7", DpadAxisY(buttons));
  }

  static double DpadAxisX(u64 buttons) {
    if (buttons & HidNpadButton_Left) {
      return -kDpadMax;
    }
    if (buttons & HidNpadButton_Right) {
      return kDpadMax;
    }
    return 0.0;
  }

  static double DpadAxisY(u64 buttons) {
    // Отрицательное - вверх, так ждёт normalizeDpadAxis.
    if (buttons & HidNpadButton_Up) {
      return -kDpadMax;
    }
    if (buttons & HidNpadButton_Down) {
      return kDpadMax;
    }
    return 0.0;
  }

  void SendAxis(const char* key, double value) {
    const std::string name(key);
    const double previous = LastValue(name);
    if (std::fabs(previous - value) <= kAxisEpsilon && value != 0.0) {
      return;
    }
    if (previous == value) {
      return;
    }
    Remember(name, value);
    PadEvent event;
    event.analog = true;
    event.key = name;
    event.value = value;
    Push(event);
  }

  PadState pad_{};
  HidNpadIdType npad_id_ = HidNpadIdType_No1;
  HidVibrationDeviceHandle handles_[kVibrationHandles] = {};
  bool rumble_ok_ = false;
  std::string id_;

  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};

  std::mutex mutex_;
  std::deque<PadEvent> events_;
  int64_t rumble_until_ns_ = 0;
};

}  // namespace
}  // namespace flutter_libnx

extern "C" {

bool flutter_libnx_gamepad_init(std::string* error) {
  return flutter_libnx::Gamepad::Instance().Initialize(error);
}

void flutter_libnx_gamepad_shutdown(void) {
  flutter_libnx::Gamepad::Instance().Shutdown();
}

std::string flutter_libnx_gamepad_id(void) {
  return flutter_libnx::Gamepad::Instance().Id();
}

std::string flutter_libnx_gamepad_name(void) {
  return flutter_libnx::Gamepad::Instance().Name();
}

bool flutter_libnx_gamepad_has_rumble(void) {
  return flutter_libnx::Gamepad::Instance().HasRumble();
}

bool flutter_libnx_gamepad_rumble(float low, float high, int duration_ms) {
  return flutter_libnx::Gamepad::Instance().Rumble(low, high, duration_ms);
}

void flutter_libnx_gamepad_stop_rumble(void) {
  flutter_libnx::Gamepad::Instance().StopRumble();
}

bool flutter_libnx_gamepad_pop_event(flutter_libnx::PadEvent* event) {
  return flutter_libnx::Gamepad::Instance().PopEvent(event);
}

}  // extern "C"