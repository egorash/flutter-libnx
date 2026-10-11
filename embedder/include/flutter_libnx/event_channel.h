// EventChannel-Helper: einheitliche Abwicklung von Dart-EventChannels
// (StandardMethodCodec) auf der Plattformseite.
//
// Das EventChannel-Protokoll besteht aus listen/cancel, die Dart an den
// Embedder schickt, und aus Events bzw. Fehlern, die der Embedder zuruecksendet.
// Dieser Helper kapselt beides und fuehrt Stream-Zustand; er ist der Baustein,
// auf dem geolocator und kuenftige Streaming-Kanaele aufsetzen (statt jede
// Stelle mit eigenem listen/cancel-Vergleich und eigener Response-Logik zu
// duplizieren).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "embedder.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {

class EventChannel {
 public:
  // name ist der Kanalname exakt wie auf der Dart-Seite
  // (EventChannel(name, StandardMethodCodec())).
  explicit EventChannel(std::string name);

  // Nimmt eine eingehende Plattform-Nachricht entgegen. true heisst:
  // konsumiert (listen/cancel wurden beantwortet). Falsches Format oder
  // unbekannte Methode -> false, der Aufrufer behandelt den Kanal dann als
  // nicht-zustellbar.
  bool HandleMessage(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                     const FlutterPlatformMessage* message);

  bool IsListening() const { return listening_; }

  // Stoesst ein Event nach Dart ab (Erfolgshuelle um data). Ohne aktiven
  // Listener passiert nichts - dieselbe Semantik wie auf den Desktop-Embeddern.
  void SendEvent(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                 const StdValue& data) const;

  // Stoesst eine Fehlerhuelle ab; onError auf der Dart-Seite wird gerufen.
  void SendError(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                 const std::string& code,
                 const std::string& detail) const;

 private:
  void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
               const FlutterPlatformMessage* message,
               const std::vector<uint8_t>& payload) const;

  std::string name_;
  bool listening_ = false;
};

}  // namespace flutter_libnx