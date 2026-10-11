// Horizon-Shim fuer image_cropper.
//
// image_cropper braucht einen echten Klassen-Tausch: Der Default
// `MethodChannelImageCropper` laeuft gegen einen Method-Channel, den der
// Embedder nicht bedient, und wirft MissingPluginException. Andere
// Plattformen setzen `ImageCropperPlatform.instance` ueber ihr registriertes
// Plugin - auf Horizon registriert sich keins.
//
// Gewuenschtes Verhalten (Konsole): es gibt kein Cropper-UI. cropImage()
// liefert das Original (sourcePath) zurueck, damit die App den Zuschnitt
// lokal machen kann (die Datei selbst bleibt unveraendert). Das ist bewusst
// KEINE stille Erfolgsmeldung - die App rechnet mit einem CroppedFile und
// bekommt es, nur eben unbeschnitten.
//
// Einbindung durch build-dart-app.sh (Horizon-Registrant): Kopie nach
// generated/ + Import + registerHorizonShim() im erzeugten horizon_main.dart.
// Den READY-Marker erst setzen, wenn die Implementierung unten komplett ist.

import 'package:image_cropper_platform_interface/image_cropper_platform_interface.dart';

/// Ersetzt [ImageCropperPlatform.instance] auf Horizon.
class HorizonImageCropper extends ImageCropperPlatform {
  HorizonImageCropper() : super();

  @override
  Future<CroppedFile?> cropImage({
    required String sourcePath,
    int? maxWidth,
    int? maxHeight,
    CropAspectRatio? aspectRatio,
    ImageCompressFormat compressFormat = ImageCompressFormat.jpg,
    int compressQuality = 90,
    List<PlatformUiSettings>? uiSettings,
  }) async {
    // TODO(horizon): ggf. hier wirklich zuschneiden; vorerst Original
    // durchreichen - der Editor der App macht den Zuschnitt lokal.
    return CroppedFile(sourcePath);
  }

  @override
  Future<CroppedFile?> recoverImage() async {
    // Android-Only-Konzept (verlorenes Ergebnis nach Activity-Tod).
    return null;
  }
}

/// Wird vom generierten Horizon-Registranten aufgerufen, sobald dieser Shim
/// den READY-Marker traegt. Aktuell bewusst auskommentiert - siehe Kopf.
void registerHorizonShim() {
  // ImageCropperPlatform.instance = HorizonImageCropper();
}