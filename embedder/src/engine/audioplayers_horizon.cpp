// Канал плагина audioplayers (xyz.luan/audioplayers) и его потоковые
// каналы на каждого игрока.
//
// Протокол разобран по исходникам пакета, а не по догадкам:
//
//   MethodChannel  xyz.luan/audioplayers          - команды (StandardMethodCodec)
//   EventChannel   xyz.luan/audioplayers/events/<playerId>
//                                               - события игрока
//   MethodChannel  xyz.luan/audioplayers.global   - контекст аудио
//
// Три обстоятельства, на которых держится реализация:
//
// 1. Идентификатор игрока приходит строкой UUID (audioplayer.dart:150,
//    uuid v4), а не числом. Значит нужна таблица playerId -> голос audren.
// 2. Dart ждёт события audio.onPrepared в _completePrepared
//    (audioplayer.dart:358) с таймаутом 30 секунд. Без события каждый
//    play() встал бы на полминуты, поэтому готовность уходит событием.
// 3. Путь к файлу приходит URL-кодированным (UriCoder.encodeOnce в
//    setSourceUrl), поэтому перед fopen его нужно декодировать.
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "embedder.h"
#include "flutter_libnx/audio_horizon.h"
#include "flutter_libnx/event_channel.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {
namespace {

const char kChannel[] = "xyz.luan/audioplayers";
const char kStreamPrefix[] = "xyz.luan/audioplayers/events/";

using flutter_libnx::StdValue;

// Таблица playerId (UUID) -> номер голоса. Живёт ровно столько же, сколько
// сам игрок: dispose освобождает голос и стирает запись.
std::map<std::string, int>& Players() {
  static std::map<std::string, int> players;
  return players;
}

std::map<int, EventChannel>& Streams() {
  static std::map<int, EventChannel> streams;
  return streams;
}

// Готовность игроков, событие о которой ещё не доставлено. Сбрасывается по
// факту отправки, поэтому повторов не бывает.
std::map<int, bool>& PendingPrepared() {
  static std::map<int, bool> pending;
  return pending;
}

std::mutex& TableMutex() {
  static std::mutex mutex;
  return mutex;
}

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

void RespondError(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                  const FlutterPlatformMessage* message,
                  const std::string& code,
                  const std::string& detail) {
  if (message->response_handle == nullptr) {
    return;
  }
  const std::vector<uint8_t> payload = EncodeStdError(code, detail);
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

std::string PlayerKey(const StdValue& args) {
  const StdValue* value = Find(args, "playerId");
  if (value == nullptr || value->type != StdValue::Type::kString) {
    return std::string();
  }
  return value->as_string;
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

// Обратное кодирование для пути к файлу. Путь приходит через
// UriCoder.encodeOnce, а нам нужен путь, который переживёт fopen.
std::string PercentDecode(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const std::string hex = text.substr(i + 1, 2);
      char* end = nullptr;
      const long value = strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        out.push_back(static_cast<char>(value));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

int VoiceOf(const std::string& player) {
  auto it = Players().find(player);
  return it == Players().end() ? -1 : it->second;
}

// Отправляет событие игрока в его поток. Формат - словарь с полями
// event/value, как ждёт Dart (audioplayers_platform.dart:242-265).
void SendStreamEvent(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                     EventChannel& stream,
                     const std::string& name,
                     const StdValue& value) {
  StdValue payload = StdValue::Map();
  payload.as_map.emplace_back(StdValue::String("event"), StdValue::String(name));
  payload.as_map.emplace_back(StdValue::String("value"), value);
  stream.SendEvent(engine, payload);
}

}  // namespace
}  // namespace flutter_libnx

extern "C" {

bool flutter_libnx_audioplayers_handle(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message,
    const std::string& method,
    const flutter_libnx::StdValue& args) {
  using namespace flutter_libnx;

  // Глобальный контекст на Switch не имеет смысла: один вывод звука,
  // постоянная громкость. Успех вместо MissingPluginException.
  if (method == "resumeAudioContext" || method == "suspendAudioContext" ||
      method == "setApplicationContext" || method == "getApplicationContext") {
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "getActivePlayers") {
    Respond(engine, message, StdValue::List());
    return true;
  }
  if (method == "getDevices" || method == "setDeviceId") {
    Respond(engine, message, StdValue::Null());
    return true;
  }

  const std::string player = PlayerKey(args);

  if (method == "create") {
    const int voice = flutter_libnx_audio_create_player();
    if (voice < 0) {
      RespondError(engine, message, "no_voices", "keine freie Audio-Stimme");
      return true;
    }
    std::lock_guard<std::mutex> guard(TableMutex());
    Players()[player] = voice;
    LOG_INFO("audioplayers: Player %s -> Stimme %d", player.c_str(), voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }

  if (player.empty()) {
    RespondError(engine, message, "bad_player", "playerId fehlt");
    return true;
  }

  int voice = -1;
  {
    std::lock_guard<std::mutex> guard(TableMutex());
    voice = VoiceOf(player);
  }
  if (voice < 0) {
    RespondError(engine, message, "unknown_player",
                 "kein create fuer " + player);
    return true;
  }

  if (method == "dispose") {
    flutter_libnx_audio_release_player(voice);
    std::lock_guard<std::mutex> guard(TableMutex());
    Players().erase(player);
    Streams().erase(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "release") {
    flutter_libnx_audio_release_source(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "setSourceUrl" || method == "setSourceBytes") {
    const std::string path = PercentDecode(Text(args, "url"));
    if (path.empty()) {
      RespondError(engine, message, "bad_source", "setSourceUrl ohne url");
      return true;
    }
    std::string error;
    flutter_libnx_audio_set_source(voice, path, &error);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "play" || method == "resume") {
    flutter_libnx_audio_play(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "pause") {
    flutter_libnx_audio_pause(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "stop") {
    flutter_libnx_audio_stop(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "setVolume") {
    flutter_libnx_audio_set_volume(
        voice, static_cast<float>(Number(args, "volume", 1.0)));
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "setPlaybackRate") {
    flutter_libnx_audio_set_rate(
        voice, static_cast<float>(Number(args, "playbackRate", 1.0)));
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "setBalance") {
    // Стерео-баланс осмысленно не реализуем: голос и так моно в оба канала.
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "seek") {
    // Перемотка назад делается честно - остановкой; позиция 0.
    flutter_libnx_audio_stop(voice);
    Respond(engine, message, StdValue::Null());
    return true;
  }
  if (method == "getCurrentPosition") {
    Respond(engine, message,
            StdValue::Int(flutter_libnx_audio_position_ms(voice)));
    return true;
  }
  if (method == "getDuration") {
    Respond(engine, message,
            StdValue::Int(flutter_libnx_audio_duration_ms(voice)));
    return true;
  }
  if (method == "setReleaseMode" || method == "setPlayerMode" ||
      method == "setAudioContext" || method == "emitLog") {
    Respond(engine, message, StdValue::Null());
    return true;
  }

  RespondError(engine, message, "unsupported",
               "audioplayers: Methode nicht bedient: " + method);
  return true;
}

bool flutter_libnx_audioplayers_stream_handle(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message,
    const std::string& channel_name) {
  using namespace flutter_libnx;

  const std::string prefix = kStreamPrefix;
  if (channel_name.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  const std::string player = channel_name.substr(prefix.size());

  int voice = -1;
  {
    std::lock_guard<std::mutex> guard(TableMutex());
    voice = VoiceOf(player);
  }
  if (voice < 0) {
    // Канал пришёл раньше create (или после dispose) - отвечаем ошибкой,
    // как это делает Dart-обёртка для неизвестного потока.
    return false;
  }

  std::lock_guard<std::mutex> guard(TableMutex());
  auto& streams = Streams();
  auto it = streams.find(voice);
  if (it == streams.end()) {
    it = streams.emplace(voice, EventChannel(channel_name)).first;
  }
  return it->second.HandleMessage(engine, message);
}

void flutter_libnx_audioplayers_pump(
    FLUTTER_API_SYMBOL(FlutterEngine) engine) {
  using namespace flutter_libnx;

  int voice = 0;
  bool prepared = false;
  while (flutter_libnx_audio_pop_event(&voice, &prepared)) {
    std::lock_guard<std::mutex> guard(TableMutex());
    if (prepared) {
      // Dart ещё не подписан: create() в пакете поток вообще не создаёт,
      // а listen уходит только при первой подписке. Отправленное событие
      // пропало бы, и play() ждал бы своих 30 секунд. Поэтому готовность
      // запоминаем и отдаём, как только поток действительно слушают.
      PendingPrepared()[voice] = true;
    } else {
      // Завершение можно потерять: кто не слушал, того и не жалко.
      auto it = Streams().find(voice);
      if (it == Streams().end() || !it->second.IsListening()) {
        continue;
      }
      SendStreamEvent(engine, it->second, "audio.onComplete", StdValue::Null());
    }
  }

  std::lock_guard<std::mutex> guard(TableMutex());
  for (auto& entry : PendingPrepared()) {
    if (!entry.second) {
      continue;
    }
    auto it = Streams().find(entry.first);
    if (it == Streams().end() || !it->second.IsListening()) {
      continue;
    }
    SendStreamEvent(engine, it->second, "audio.onPrepared",
                    StdValue::Bool(true));
    entry.second = false;
  }
}

}  // extern "C"