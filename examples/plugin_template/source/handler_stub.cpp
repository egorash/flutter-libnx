// Referenz-Handler fuer ein eigenes Plugin - Beispiel-Template.
//
// DIESE DATEI IST BEWUSST NICHT ANGESCHALTET: Der Handler wird hier nur
// mituebersetzt (die Makefile-Liste source/generated/... nimmt alles aus
// source/ auf), damit die Referenz gegen die echten Embedder-Header baut
// und das Beispiel trotzdem laeuft. Aufgerufen wird er nicht - der
// Kanal-Dispatcher steckt im Embedder (plugins_horizon.cpp).
//
// Ein neues Plugin anbinden heisst ZWEI Kopien (siehe README.md):
//   1. diese Funktion (oder eine eigene) nach plugins_horizon.cpp in den
//      anonymen namespace kopieren,
//   2. im Kanal-Register MakeRegistry() eine Zeile ergaenzen:
//        r.method["plugins.flutter.io/plugin_template"] =
//            flutter_libnx_plugin_template_handler;
// Danach Engine-Neubau (rebuild-all.sh) - der Dart-Teil des Beispiels ruft
// genau diesen Kanal (lib/main.dart) und zeigt das Ergebnis an.
//
// Alternativ B-Pfad (Dart-Shim statt C++-Handler): siehe
// dart_helpers/horizon_plugins/ und build-dart-app.sh (Marker
// // HORIZON_SHIM: READY).

#include <switch.h>

#include <string>

#include "embedder.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {

namespace {

// Antwort auf den Platform-Message-Handle schreiben (StandardMethodCodec hat
// der Dispatcher schon dekodiert; hier kommt nur noch die Antwort raus).
void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
             const FlutterPlatformMessage* message,
             const std::vector<uint8_t>& payload) {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

}  // namespace

// Signatur entspricht MethodHandler in plugins_horizon.cpp:
//   (engine, message, method, args) -> bool; true = beantwortet.
// Bewusst kein static/anonym: Der Embedder referenziert den Namen nur, wenn
// der Anwender das Register ergaenzt hat; hier bleibt die Funktion unbenutzt.
extern "C" bool flutter_libnx_plugin_template_handler(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message,
    const std::string& method,
    const StdValue& /*args*/) {
  if (method == "getGreeting") {
    Respond(engine, message, EncodeStdSuccess(StdValue::String(
                                 "Hallo von plugins.flutter.io/plugin_template")));
    return true;
  }
  LOG_WARN("plugin_template: unbekannte Methode '%s'", method.c_str());
  return false;  // ehrlich: leere Antwort -> MissingPluginException
}

}  // namespace flutter_libnx