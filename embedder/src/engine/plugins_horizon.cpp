// Gruppe-B-Plugins: path_provider und shared_preferences.
//
// Warum diese zwei zuerst: Sie tauchen in nahezu jeder Flutter-App auf
// (docs/target-apps.md), und der erste Lauf einer echten fremden App
// (rezkonv_app) ist exakt an ihnen gescheitert - shared_preferences.getAll
// beim Start, path_provider.getTemporaryDirectory vor jedem
// Datenbankzugriff von drift.
//
// Beide Kanäle sprechen den StandardMethodCodec (Binärformat), nicht JSON -
// deshalb existiert flutter_libnx/standard_message_codec.h.
//
// Ablage: /switch/flutter_apps/<app-id>/ auf der SD-Karte. Der Pfad ist
// absichtlich ohne "sdmc:"-Präfix - hbmenu startet mit sdmc als
// Standardgerät, und ein POSIX-reiner Pfad übersteht die Pfadarithmetik
// des Dart-path-Pakets (p.join, p.normalize, isAbsolute) unbeschädigt.
//
// shared_preferences persistiert als codec-kodierte Map in einer Datei:
// dasselbe Format wie auf dem Draht, kein eigener Parser, atomar über
// Nebendatei + rename.
//
// Alle Handler laufen auf dem Plattform-Thread (der Hauptschleife) - keine
// Nebenläufigkeit, keine Sperren.

#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#include "embedder.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {

namespace {

// Kommt aus dem Build (Makefile: -DFLUTTER_LIBNX_APP_ID="<target>") -
// jede App bekommt so ihr eigenes Datenverzeichnis, ohne dass jemand
// daran denken muss.
#ifndef FLUTTER_LIBNX_APP_ID
#define FLUTTER_LIBNX_APP_ID "flutter_app"
#endif
constexpr const char* kAppId = FLUTTER_LIBNX_APP_ID;

std::string BaseDir() {
  return std::string("/switch/flutter_apps/") + kAppId;
}

// mkdir -p. FAT kennt keine Rechte, der Modus ist Formsache.
bool EnsureDir(const std::string& path) {
  std::string partial;
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') {
      partial = path.substr(0, i);
      if (mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("mkdir %s: errno %d", partial.c_str(), errno);
        return false;
      }
    }
  }
  return true;
}

void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
             const FlutterPlatformMessage* message,
             const std::vector<uint8_t>& payload) {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

// --- path_provider ----------------------------------------------------------

// path_provider nimmt keine Argumente - alle Methoden sind reine Getter, der
// args-Parameter bleibt aus Einheitlichkeit im Registry-Schema ungenutzt.
bool HandlePathProvider(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                        const FlutterPlatformMessage* message,
                        const std::string& method,
                        const StdValue& /*args*/) {
  const char* subdir = nullptr;
  if (method == "getTemporaryDirectory") {
    subdir = "tmp";
  } else if (method == "getApplicationDocumentsDirectory") {
    subdir = "documents";
  } else if (method == "getApplicationSupportDirectory") {
    subdir = "support";
  } else if (method == "getLibraryDirectory") {
    subdir = "library";
  } else if (method == "getDownloadsDirectory") {
    subdir = "downloads";
  } else if (method == "getApplicationCachePath" ||
             method == "getApplicationCacheDirectory") {
    subdir = "cache";
  } else if (method == "getExternalStorageDirectory") {
    // Kein Zweitspeicher auf der Konsole; null ist die dokumentierte Antwort.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  } else if (method == "getExternalCacheDirectories" ||
             method == "getExternalStorageDirectories") {
    Respond(engine, message, EncodeStdSuccess(StdValue::List()));
    return true;
  } else {
    LOG_WARN("path_provider: unbekannte Methode '%s'", method.c_str());
    return false;  // leere Antwort -> MissingPluginException, ehrlich
  }

  const std::string path = BaseDir() + "/" + subdir;
  if (!EnsureDir(path)) {
    Respond(engine, message,
            EncodeStdError("io", "Verzeichnis nicht anlegbar: " + path));
    return true;
  }
  Respond(engine, message, EncodeStdSuccess(StdValue::String(path)));
  return true;
}

// --- shared_preferences -----------------------------------------------------

StdValue g_prefs = StdValue::Map();
bool g_prefs_loaded = false;

std::string PrefsPath() {
  return BaseDir() + "/shared_preferences.std";
}

void LoadPrefs() {
  if (g_prefs_loaded) {
    return;
  }
  g_prefs_loaded = true;
  g_prefs = StdValue::Map();

  FILE* f = fopen(PrefsPath().c_str(), "rb");
  if (f == nullptr) {
    return;  // Erste Benutzung: leer ist der richtige Zustand.
  }
  std::vector<uint8_t> data;
  uint8_t chunk[4096];
  size_t n = 0;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    data.insert(data.end(), chunk, chunk + n);
  }
  fclose(f);

  size_t cursor = 0;
  StdValue loaded;
  if (DecodeStdValue(data.data(), data.size(), &cursor, &loaded) &&
      loaded.type == StdValue::Type::kMap) {
    g_prefs = std::move(loaded);
  } else {
    LOG_ERROR("shared_preferences: Datei unlesbar, beginne leer");
  }
}

bool SavePrefs() {
  if (!EnsureDir(BaseDir())) {
    return false;
  }
  std::vector<uint8_t> data;
  EncodeStdValue(g_prefs, &data);

  const std::string path = PrefsPath();
  const std::string tmp = path + ".neu";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    LOG_ERROR("shared_preferences: %s nicht schreibbar (errno %d)",
              tmp.c_str(), errno);
    return false;
  }
  const bool written = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  if (!written) {
    remove(tmp.c_str());
    return false;
  }
  // FAT hat kein atomares Ersetzen; Löschen + Umbenennen ist das Beste, was
  // die Karte hergibt. Schlimmster Fall bei Stromverlust: alte Datei weg,
  // neue liegt unter dem Nebennamen - der Lader beginnt dann leer.
  remove(path.c_str());
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    LOG_ERROR("shared_preferences: rename nach %s: errno %d", path.c_str(),
              errno);
    return false;
  }
  return true;
}

const StdValue* MapGet(const StdValue& map, const std::string& key) {
  for (const auto& entry : map.as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

void MapSet(StdValue* map, const std::string& key, StdValue value) {
  for (auto& entry : map->as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      entry.second = std::move(value);
      return;
    }
  }
  map->as_map.emplace_back(StdValue::String(key), std::move(value));
}

bool HandleSharedPreferences(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                             const FlutterPlatformMessage* message,
                             const std::string& method,
                             const StdValue& args) {
  LoadPrefs();

  if (method == "getAll") {
    Respond(engine, message, EncodeStdSuccess(g_prefs));
    return true;
  }

  if (method == "setBool" || method == "setInt" || method == "setDouble" ||
      method == "setString" || method == "setStringList") {
    const StdValue* key = MapGet(args, "key");
    const StdValue* value = MapGet(args, "value");
    if (key == nullptr || key->type != StdValue::Type::kString ||
        value == nullptr) {
      Respond(engine, message,
              EncodeStdError("argument", "key/value fehlt bei " + method));
      return true;
    }
    MapSet(&g_prefs, key->as_string, *value);
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "remove") {
    const StdValue* key = MapGet(args, "key");
    if (key != nullptr && key->type == StdValue::Type::kString) {
      auto& entries = g_prefs.as_map;
      for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].first.type == StdValue::Type::kString &&
            entries[i].first.as_string == key->as_string) {
          entries.erase(entries.begin() + i);
          break;
        }
      }
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "clear") {
    // Wie die echten Implementierungen: nur die "flutter."-Einträge, fremde
    // bleiben stehen.
    auto& entries = g_prefs.as_map;
    for (size_t i = entries.size(); i > 0; i--) {
      const StdValue& k = entries[i - 1].first;
      if (k.type == StdValue::Type::kString &&
          k.as_string.rfind("flutter.", 0) == 0) {
        entries.erase(entries.begin() + (i - 1));
      }
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "commit") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(true)));
    return true;
  }

  LOG_WARN("shared_preferences: unbekannte Methode '%s'", method.c_str());
  return false;
}

// --- url_launcher -----------------------------------------------------------
//
// Der Systembrowser der Konsole (NetFront, WebKit-basiert) laeuft als
// Vollbild-Applet: webPageCreate + webConfigShow, die Domain der URL wird
// automatisch freigeschaltet (libnx web.h:315). Wie beim swkbd blockiert
// die Anzeige die Hauptschleife - das Applet uebernimmt den Bildschirm
// ohnehin. Die Antwort geht deshalb VOR dem Oeffnen raus.
//
// Nur http/https: Andere Schemata (mailto, tel, ...) kann die Konsole
// nicht darstellen; canLaunch sagt das ehrlich.

bool IsWebUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool HandleUrlLauncher(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                       const FlutterPlatformMessage* message,
                       const std::string& method,
                       const StdValue& args) {
  const StdValue* url = MapGet(args, "url");
  const bool has_url =
      url != nullptr && url->type == StdValue::Type::kString;

  if (method == "canLaunch") {
    Respond(engine, message,
            EncodeStdSuccess(
                StdValue::Bool(has_url && IsWebUrl(url->as_string))));
    return true;
  }
  if (method == "launch") {
    if (!has_url || !IsWebUrl(url->as_string)) {
      Respond(engine, message,
              EncodeStdError("argument",
                             "Nur http/https lassen sich auf der Konsole "
                             "oeffnen"));
      return true;
    }
    // Erst antworten, dann blockieren (Applet uebernimmt den Bildschirm).
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(true)));

    WebCommonConfig config;
    const Result rc = webPageCreate(&config, url->as_string.c_str());
    if (R_SUCCEEDED(rc)) {
      LOG_INFO("url_launcher: oeffne %s im Systembrowser",
               url->as_string.c_str());
      webConfigShow(&config, nullptr);
    } else {
      LOG_ERROR("url_launcher: webPageCreate = 0x%08x", rc);
    }
    return true;
  }
  if (method == "closeWebView") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(true)));
    return true;
  }

  LOG_WARN("url_launcher: unbekannte Methode '%s'", method.c_str());
  return false;
}

// --- geolocator -------------------------------------------------------------
//
// Die Konsole hat kein Ortungssystem (kein GPS, kein Netzortung) - geolocator
// (14.x, Kanal flutter.baseflow.com/geolocator, StandardMethodCodec)
// antwortet daher ehrlich "denied". getCurrentPosition erreicht die App auf
// diesem Weg nie: Bei denied steigt travels_map_controller still aus
// (_getCurrentLocation -> requestPermission -> denied -> return), die Karte
// bleibt im Standardzentrum. Andere Aufrufer bekommen die explizite
// PERMISSION_DENIED-Exception.
//
// Die beiden Event-Kanaele (_updates, _service_updates) quittieren listen/
// cancel mit Erfolg, senden aber nie Ereignisse - es gibt keinen Dienst,
// der sie speisen koennte.
//
// Permission-Codes (geolocator_platform_interface 4.2.8,
// IntegerExtensions.toLocationPermission): 0=denied, 1=deniedForever,
// 2=whileInUse, 3=always.

bool HandleGeolocatorEventChannel(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                                  const FlutterPlatformMessage* message) {
  std::string method;
  StdValue args;
  if (!DecodeStdMethodCall(message->message, message->message_size, &method,
                           &args)) {
    return false;
  }
  // listen/cancel sind das gesamte EventChannel-Protokoll.
  if (method == "listen" || method == "cancel") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  return false;
}

bool HandleGeolocator(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                      const FlutterPlatformMessage* message,
                      const std::string& method,
                      const StdValue& args) {
  if (method == "checkPermission" || method == "requestPermission") {
    // Kein GPS: ehrlich "denied" (0). Die App fragt daraufhin nicht weiter.
    Respond(engine, message, EncodeStdSuccess(StdValue::Int(0)));
    return true;
  }
  if (method == "isLocationServiceEnabled") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(false)));
    return true;
  }
  if (method == "getLastKnownPosition") {
    // Es gibt keine bekannte Position - null ist die ehrliche Antwort.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  if (method == "getLocationAccuracy") {
    // LocationAccuracyStatus.values[0] == reduced; ohne Hardware der einzige
    // nicht-werfende int (der Dart-Code indiziert das Enum direkt).
    Respond(engine, message, EncodeStdSuccess(StdValue::Int(0)));
    return true;
  }
  if (method == "getCurrentPosition") {
    Respond(engine, message,
            EncodeStdError("PERMISSION_DENIED",
                           "Switch hat kein Ortungssystem (geolocator)"));
    return true;
  }
  if (method == "openAppSettings" || method == "openLocationSettings") {
    // Es gibt keine Ortungseinstellungen zum Oeffnen.
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(false)));
    return true;
  }
  if (method == "requestTemporaryFullAccuracy") {
    // iOS-only-Konzept, hier nicht existent.
    Respond(engine, message,
            EncodeStdError("unimplemented", "requestTemporaryFullAccuracy ist "
                                             "auf Horizon nicht verfuegbar"));
    return true;
  }

  LOG_WARN("geolocator: unbekannte Methode '%s'", method.c_str());
  return false;
}

// --- flutter_secure_storage -------------------------------------------------
//
// EHRLICHKEITSHINWEIS: "Secure" ist auf dieser Plattform nicht leistbar.
// Die SD-Karte ist FAT, es gibt keinen Schluesselbund und keine
// Nutzerkonten - die Werte liegen im KLARTEXT unter
// /switch/flutter_apps/<id>/secure_storage.std. Das ist trotzdem die
// richtige Abwaegung: Die Alternative (MissingPluginException) liess
// rezkonv mit einem LateInitializationError haengen, und die Karte
// steckt in der eigenen Konsole. Wer echte Geheimnisse hat, gehoert
// nicht auf eine Homebrew-SD.
//
// Kanal plugins.it_nomads.com/flutter_secure_storage, StandardMethodCodec.
// Argumente kommen als Map, der Schluessel heisst "key", der Wert "value";
// zusaetzlich schickt das Plugin "options", die hier bedeutungslos sind.

StdValue g_secure = StdValue::Map();
bool g_secure_loaded = false;

std::string SecurePath() {
  return BaseDir() + "/secure_storage.std";
}

void LoadSecure() {
  if (g_secure_loaded) {
    return;
  }
  g_secure_loaded = true;
  g_secure = StdValue::Map();

  FILE* f = fopen(SecurePath().c_str(), "rb");
  if (f == nullptr) {
    return;
  }
  std::vector<uint8_t> data;
  uint8_t chunk[4096];
  size_t n = 0;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    data.insert(data.end(), chunk, chunk + n);
  }
  fclose(f);

  size_t cursor = 0;
  StdValue loaded;
  if (DecodeStdValue(data.data(), data.size(), &cursor, &loaded) &&
      loaded.type == StdValue::Type::kMap) {
    g_secure = std::move(loaded);
  }
}

bool SaveSecure() {
  if (!EnsureDir(BaseDir())) {
    return false;
  }
  std::vector<uint8_t> data;
  EncodeStdValue(g_secure, &data);
  const std::string path = SecurePath();
  const std::string tmp = path + ".neu";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    return false;
  }
  const bool written = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  if (!written) {
    remove(tmp.c_str());
    return false;
  }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

bool HandleSecureStorage(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                         const FlutterPlatformMessage* message,
                         const std::string& method,
                         const StdValue& args) {
  LoadSecure();
  const StdValue* key = MapGet(args, "key");
  const bool has_key =
      key != nullptr && key->type == StdValue::Type::kString;

  if (method == "read") {
    const StdValue* value =
        has_key ? MapGet(g_secure, key->as_string) : nullptr;
    Respond(engine, message,
            EncodeStdSuccess(value != nullptr ? *value : StdValue::Null()));
    return true;
  }
  if (method == "containsKey") {
    Respond(engine, message,
            EncodeStdSuccess(StdValue::Bool(
                has_key && MapGet(g_secure, key->as_string) != nullptr)));
    return true;
  }
  if (method == "readAll") {
    Respond(engine, message, EncodeStdSuccess(g_secure));
    return true;
  }
  if (method == "write") {
    const StdValue* value = MapGet(args, "value");
    if (has_key && value != nullptr) {
      MapSet(&g_secure, key->as_string, *value);
      SaveSecure();
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  if (method == "delete") {
    if (has_key) {
      auto& entries = g_secure.as_map;
      for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].first.type == StdValue::Type::kString &&
            entries[i].first.as_string == key->as_string) {
          entries.erase(entries.begin() + i);
          break;
        }
      }
      SaveSecure();
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  if (method == "deleteAll") {
    g_secure = StdValue::Map();
    SaveSecure();
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }

  LOG_WARN("secure_storage: unbekannte Methode '%s'", method.c_str());
  return false;
}

// --- file_picker ------------------------------------------------------------
//
// Einen Datei-Browser als eigene Vollbild-UI gibt es (noch) nicht. Die
// Switch-Konvention lautet stattdessen: Dateien, die eine App importieren
// soll, liegen in /switch/flutter_apps/<app-id>/import/ auf der SD-Karte.
// Ein "Datei wählen" liefert den Inhalt dieses Ordners - bei
// Einzelauswahl die neueste Datei, bei Mehrfachauswahl alle passenden.
// Ist der Ordner leer, kommt null zurück, was file_picker als "abgebrochen"
// versteht - die App zeigt dann ihren normalen Kein-Ergebnis-Weg.

std::string ImportDir() {
  return BaseDir() + "/import";
}

bool HasAllowedExtension(const std::string& name,
                         const std::vector<std::string>& extensions) {
  if (extensions.empty()) {
    return true;
  }
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = name.substr(dot + 1);
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  for (const std::string& allowed : extensions) {
    if (ext == allowed) {
      return true;
    }
  }
  return false;
}

// ACHTUNG Codec-Falle: FilePickerIO waehlt den Codec nach Plattform
// (file_picker_io.dart:7) - auf "Linux", als das wir uns melden, spricht
// der Kanal JSONMethodCodec, NICHT den Standard-Codec. Die erste Fassung
// dieses Handlers dekodierte binaer und verwarf jede Nachricht.
void RespondJson(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                 const FlutterPlatformMessage* message,
                 const std::string& payload) {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(
      engine, message->response_handle,
      reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

std::string JsonEscapeMinimal(const std::string& s) {
  std::string out;
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

bool HandleFilePicker(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                      const FlutterPlatformMessage* message) {
  const std::string msg(reinterpret_cast<const char*>(message->message),
                        message->message_size);
  const auto method_is = [&](const char* m) {
    return msg.find(std::string("\"method\":\"") + m + "\"") !=
           std::string::npos;
  };

  if (method_is("clear")) {
    RespondJson(engine, message, "[true]");
    return true;
  }
  EnsureDir(ImportDir());
  if (method_is("dir")) {
    RespondJson(engine, message, "[\"" + ImportDir() + "\"]");
    return true;
  }

  // any / custom / media / image / video / audio - alle laufen auf eine
  // Dateiliste hinaus.
  std::vector<std::string> extensions;
  const size_t ext_start = msg.find("\"allowedExtensions\":[");
  if (ext_start != std::string::npos) {
    size_t i = ext_start + 21;
    while (i < msg.size() && msg[i] != ']') {
      if (msg[i] == '"') {
        const size_t end = msg.find('"', i + 1);
        if (end == std::string::npos) {
          break;
        }
        std::string ext = msg.substr(i + 1, end - i - 1);
        for (char& c : ext) {
          if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
          }
        }
        extensions.push_back(ext);
        i = end + 1;
      } else {
        i++;
      }
    }
  }
  const bool allow_multiple =
      msg.find("\"allowMultipleSelection\":true") != std::string::npos;

  struct Candidate {
    std::string name;
    int64_t size;
    int64_t mtime;
  };
  std::vector<Candidate> found;

  DIR* dir = opendir(ImportDir().c_str());
  if (dir != nullptr) {
    while (dirent* entry = readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") {
        continue;
      }
      if (!HasAllowedExtension(name, extensions)) {
        continue;
      }
      struct stat st = {};
      const std::string full = ImportDir() + "/" + name;
      if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        continue;
      }
      found.push_back({name, static_cast<int64_t>(st.st_size),
                       static_cast<int64_t>(st.st_mtime)});
    }
    closedir(dir);
  }

  if (found.empty()) {
    LOG_INFO("file_picker: keine passenden Dateien in %s - dorthin kopieren",
             ImportDir().c_str());
    RespondJson(engine, message, "[null]");
    return true;
  }

  if (!allow_multiple) {
    // Neueste Datei gewinnt - wer gerade etwas auf die Karte kopiert hat,
    // meint genau diese.
    size_t best = 0;
    for (size_t i = 1; i < found.size(); i++) {
      if (found[i].mtime > found[best].mtime) {
        best = i;
      }
    }
    found = {found[best]};
  }

  std::string list = "[";
  for (size_t i = 0; i < found.size(); i++) {
    const Candidate& file = found[i];
    if (i > 0) {
      list += ",";
    }
    list += "{\"path\":\"" + JsonEscapeMinimal(ImportDir() + "/" + file.name) +
            "\",\"name\":\"" + JsonEscapeMinimal(file.name) +
            "\",\"size\":" + std::to_string(file.size) +
            ",\"bytes\":null,\"identifier\":null}";
    LOG_INFO("file_picker: liefere %s (%lld Bytes)", file.name.c_str(),
             static_cast<long long>(file.size));
  }
  list += "]";
  RespondJson(engine, message, "[" + list + "]");
  return true;
}

// --- image_picker -----------------------------------------------------------
//
// Kanal plugins.flutter.io/image_picker (StandardMethodCodec). Die App ruft
// (MediaPickerSheet -> pickFromDevice) pickImage mit source gallery|camera:
// Kamera gibt es nicht, "Galerie" ist wie bei file_picker der Import-Ordner
// /switch/flutter_apps/<app-id>/import/ - geliefert wird der Pfad der
// neuesten Bilddatei. Leerer Ordner oder Kamera -> null, der normale
// "Abgebrochen"-Fall des Protokolls, den der Wizard sauber verarbeitet
// (navigator.pop(null)). Eine PlatformException waere hier gefaehrlich:
// MediaPickerSheet faengt nichts (nur try/finally) - der Fehler wuerde den
// Assistenten zerlegen. Deshalb: nie werfen, immer null oder Pfad.
//
// Bildformate: jpg, jpeg, png, gif, webp, bmp.

std::string NewestImportFor(const std::vector<std::string>& extensions) {
  EnsureDir(ImportDir());
  DIR* dir = opendir(ImportDir().c_str());
  if (dir == nullptr) {
    return "";
  }
  std::string best;
  int64_t best_mtime = -1;
  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    if (!HasAllowedExtension(name, extensions)) {
      continue;
    }
    struct stat st = {};
    const std::string full = ImportDir() + "/" + name;
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }
    if (st.st_mtime > best_mtime) {
      best_mtime = static_cast<int64_t>(st.st_mtime);
      best = full;
    }
  }
  closedir(dir);
  return best;
}

bool HandleImagePicker(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                       const FlutterPlatformMessage* message,
                       const std::string& method,
                       const StdValue& args) {
  if (method == "pickImage" || method == "pickMultiImage") {
    const StdValue* source = MapGet(args, "source");
    const bool camera = source != nullptr &&
                        source->type == StdValue::Type::kString &&
                        source->as_string == "camera";
    if (camera) {
      LOG_WARN("image_picker: Kamera gibt es auf der Konsole nicht -> null");
      Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
      return true;
    }
    static const std::vector<std::string> kImageExt = {
        "jpg", "jpeg", "png", "gif", "webp", "bmp"};
    const std::string path = NewestImportFor(kImageExt);
    if (path.empty()) {
      LOG_INFO("image_picker: keine Bilder in %s - dorthin kopieren",
               ImportDir().c_str());
      Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
      return true;
    }
    if (method == "pickImage") {
      LOG_INFO("image_picker: liefere %s", path.c_str());
      Respond(engine, message, EncodeStdSuccess(StdValue::String(path)));
      return true;
    }
    // pickMultiImage: Die App nutzt nur pickImage; die Liste bekommt
    // trotzdem den neuesten Eintrag statt eines leeren Ergebnisses.
    StdValue list = StdValue::List();
    list.as_list.push_back(StdValue::String(path));
    Respond(engine, message, EncodeStdSuccess(list));
    return true;
  }
  if (method == "pickVideo" || method == "pickMedia") {
    // Video/Media-Container gibt es nicht (keine eigenen Screens) - null.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }
  if (method == "retrieveLostData" || method == "getLostData") {
    // Es gibt keine verlorenen Daten - null.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }

  LOG_WARN("image_picker: unbekannte Methode '%s'", method.c_str());
  return false;
}

}  // namespace

// --- Kanal-Registry ---------------------------------------------------------
//
// Ein neuer Kanal = eine Zeile in MakeRegistry(): raw fuer Kanaele, die ihr
// Nachrichtenformat selbst dekodieren (JSON/EventChannel), method fuer Kanaele
// mit StandardMethodCodec. Der Dispatcher kennt keine if/strcmp-Kette mehr und
// buendelt Lookup + Dekodierung + Aufruf an einer Stelle.

namespace {

using RawHandler = bool (*)(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                            const FlutterPlatformMessage* message);
using MethodHandler = bool (*)(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                               const FlutterPlatformMessage* message,
                               const std::string& method,
                               const StdValue& args);

struct ChannelRegistry {
  std::unordered_map<std::string, RawHandler> raw;
  std::unordered_map<std::string, MethodHandler> method;
};

ChannelRegistry MakeRegistry() {
  ChannelRegistry r;
  // Kanäle mit eigenem Codec/Protokoll (dekodieren die Nachricht selbst).
  r.raw["miguelruivo.flutter.plugins.filepicker"] = HandleFilePicker;
  r.raw["flutter.baseflow.com/geolocator_updates"] = HandleGeolocatorEventChannel;
  r.raw["flutter.baseflow.com/geolocator_service_updates"] =
      HandleGeolocatorEventChannel;
  // StandardMethodCodec: method + args, Antwort ueber EncodeStd*.
  r.method["plugins.flutter.io/path_provider"] = HandlePathProvider;
  r.method["plugins.flutter.io/shared_preferences"] = HandleSharedPreferences;
  r.method["plugins.it_nomads.com/flutter_secure_storage"] = HandleSecureStorage;
  r.method["plugins.flutter.io/url_launcher"] = HandleUrlLauncher;
  r.method["flutter.baseflow.com/geolocator"] = HandleGeolocator;
  r.method["plugins.flutter.io/image_picker"] = HandleImagePicker;
  return r;
}

const ChannelRegistry& GetRegistry() {
  static const ChannelRegistry kRegistry = MakeRegistry();
  return kRegistry;
}

// Diagnose (Phase 0): unbekannte Kanaele beim ersten Auftreten melden, damit
// im Log sichtbar wird, welche Kanaele die App wirklich anspricht.
bool LogUnknownChannelOnce(const std::string& channel) {
  static std::set<std::string> g_logged_unknown_channels;
  if (g_logged_unknown_channels.insert(channel).second) {
    LOG_WARN("%s: unbekannter Kanal -> MissingPluginException",
             channel.c_str());
    return true;
  }
  return false;
}

}  // namespace

// Behandelt die Plugin-Kanäle. true heißt: beantwortet (oder bewusst der
// leeren Antwort des Aufrufers überlassen -> false zurückgeben!).
extern "C" bool flutter_libnx_handle_plugin_message(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message) {
  const ChannelRegistry& reg = GetRegistry();
  const std::string channel(message->channel);

  const auto raw = reg.raw.find(channel);
  if (raw != reg.raw.end()) {
    return raw->second(engine, message);
  }

  const auto method = reg.method.find(channel);
  if (method == reg.method.end()) {
    LogUnknownChannelOnce(channel);
    return false;
  }

  std::string name;
  StdValue args;
  if (!DecodeStdMethodCall(message->message, message->message_size, &name,
                           &args)) {
    LOG_WARN("%s: Nachricht nicht dekodierbar (%zu Bytes)", channel.c_str(),
             message->message_size);
    return false;
  }
  return method->second(engine, message, name, args);
}

}  // namespace flutter_libnx
