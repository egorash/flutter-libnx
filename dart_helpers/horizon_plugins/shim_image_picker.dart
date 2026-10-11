// Horizon-Shim fuer image_picker.
//
// AKTUELL NICHT NOETIG: image_picker 1.x spricht weiterhin den
// Method-Channel `plugins.flutter.io/image_picker` an, den der Embedder in
// plugins_horizon.cpp (HandleImagePicker) bedient. Die Standard-Instanz
// `MethodChannelImagePicker` reicht damit aus - es braucht KEINEN
// Plattform-Klassen-Tausch. Galerie = Import-Ordner
// sdmc:/switch/flutter_apps/<app>/import/ (wie bei file_picker).
//
// Dieser Shim ist die Absicherung fuer den Fall, dass image_picker auf das
// Pigeon-Protokoll wechselt (dann aendert sich der Kanalname und der
// C++-Handler greift nicht mehr). Den READY-Marker erst setzen, wenn das
// passiert und die Implementierung unten wirklich gebraucht wird.
//
// Einbindung durch build-dart-app.sh (Horizon-Registrant): Kopie nach
// generated/ + Import + registerHorizonShim() im erzeugten horizon_main.dart.

import 'package:image_picker_platform_interface/image_picker_platform_interface.dart';

/// Auf Pigeon umgestellte image_picker-Fassung - vorerst ungenutzt.
///
/// Verhalten wie der C++-Handler: Kamera gibt es nicht, "Galerie" ist der
/// Import-Ordner. Im Zweifel ehrlich fehlschlagen statt still zu luegen.
class HorizonImagePicker extends ImagePickerPlatform {
  HorizonImagePicker() : super();

  @override
  Future<XFile?> getImageFromSource({
    required ImageSource source,
    ImagePickerOptions options = const ImagePickerOptions(),
  }) {
    if (source == ImageSource.camera) {
      throw UnimplementedError(
        'HorizonImagePicker: Kamera gibt es auf der Konsole nicht',
      );
    }
    // TODO(horizon): Import-Ordner sdmc:/switch/flutter_apps/<app>/import/
    // durchsuchen und wie file_picker das neueste Bild zurueckgeben.
    throw UnimplementedError(
      'HorizonImagePicker.getImageFromSource: noch nicht implementiert',
    );
  }

  @override
  Future<XFile?> getVideo({
    required ImageSource source,
    CameraDevice preferredCameraDevice = CameraDevice.rear,
    Duration? maxDuration,
  }) {
    throw UnimplementedError(
      'HorizonImagePicker.getVideo: Video gibt es auf der Konsole nicht',
    );
  }
}

/// Wird vom generierten Horizon-Registranten aufgerufen, sobald dieser Shim
/// den READY-Marker traegt. Aktuell bewusst auskommentiert - siehe Kopf.
void registerHorizonShim() {
  // ImagePickerPlatform.instance = HorizonImagePicker();
}