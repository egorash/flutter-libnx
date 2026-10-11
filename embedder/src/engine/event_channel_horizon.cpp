#include "flutter_libnx/event_channel.h"

#include <utility>

#include "flutter_libnx/log.h"

namespace flutter_libnx {

EventChannel::EventChannel(std::string name) : name_(std::move(name)) {}

void EventChannel::Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                           const FlutterPlatformMessage* message,
                           const std::vector<uint8_t>& payload) const {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

bool EventChannel::HandleMessage(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                                 const FlutterPlatformMessage* message) {
  std::string method;
  StdValue args;
  if (!DecodeStdMethodCall(message->message, message->message_size, &method,
                           &args)) {
    LOG_WARN("%s: EventChannel-Nachricht nicht dekodierbar", name_.c_str());
    return false;
  }

  // listen/cancel sind das gesamte EventChannel-Protokoll.
  if (method == "listen") {
    if (!listening_) {
      LOG_INFO("%s: Stream geoeffnet (listen)", name_.c_str());
      listening_ = true;
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  if (method == "cancel") {
    if (listening_) {
      LOG_INFO("%s: Stream geschlossen (cancel)", name_.c_str());
      listening_ = false;
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }

  LOG_WARN("%s: unbekannte EventChannel-Methode '%s'", name_.c_str(),
           method.c_str());
  return false;
}

void EventChannel::SendEvent(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                             const StdValue& data) const {
  if (!listening_) {
    return;  // niemand lauscht - wie auf den Desktop-Embeddern
  }
  const std::vector<uint8_t> payload = EncodeStdSuccess(data);
  FlutterPlatformMessage message = {};
  message.struct_size = sizeof(message);
  message.channel = name_.c_str();
  message.message = payload.data();
  message.message_size = payload.size();
  // Kein response_handle: Events sind reine Sendungen ohne Antwort.
  FlutterEngineSendPlatformMessage(engine, &message);
}

void EventChannel::SendError(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                             const std::string& code,
                             const std::string& detail) const {
  if (!listening_) {
    return;
  }
  const std::vector<uint8_t> payload = EncodeStdError(code, detail);
  FlutterPlatformMessage message = {};
  message.struct_size = sizeof(message);
  message.channel = name_.c_str();
  message.message = payload.data();
  message.message_size = payload.size();
  FlutterEngineSendPlatformMessage(engine, &message);
}

}  // namespace flutter_libnx