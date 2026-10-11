// Horizon-Shim fuer webview_flutter.
//
// Warum noetig: auf Horizon registriert sich keines der webview-Pakete
// (webview_flutter_android / _wkwebview sind andere Plattformen), also bleibt
// `WebViewPlatform.instance` null. Der App-Code ruft `WebViewPlatform
// .instance!` - das gibt auf der Konsole 47x "Null check operator used on a
// null value" und laesst den FPS-Wert einbrechen, bevor ueberhaupt ein
// Kanal angesprochen wird.
//
// Diesen Shim einklinken = den Null-Check entschaerfen: instance ist gesetzt,
// und WebViewController() wirft eine ehrliche PlatformException statt der
// Null-Check-Kaskade. Es gibt auf der Switch keine Browser-Engine als View
// (der System-Browser ist ein eigener Applet, siehe docs/target-apps.md).
// TODO(horizon): Option A - URL ueber den url_launcher-Handler im
// System-Browser oeffnen; Option B (Standard) - ehrlicher Fehler, siehe unten.
//
// Einbindung durch build-dart-app.sh (Horizon-Registrant): Kopie nach
// generated/ + Import + registerHorizonShim() im erzeugten horizon_main.dart.
// Den READY-Marker erst setzen, wenn die Entscheidung A/B gefallen ist.

import 'package:flutter/services.dart';
import 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart';

/// Ersetzt [WebViewPlatform.instance] auf Horizon (bisher: null).
class HorizonWebViewPlatform extends WebViewPlatform {
  HorizonWebViewPlatform() : super();

  @override
  PlatformWebViewController createPlatformWebViewController(
    PlatformWebViewControllerCreationParams params,
  ) {
    // TODO(horizon): Option A - params.initialUrl ueber den System-Browser
    // oeffnen und einen Platzhalter-Controller zurueckgeben.
    throw PlatformException(
      code: 'unimplemented',
      message: 'WebView gibt es auf der Nintendo Switch nicht (keine '
          'Browser-Engine als View). Bitte die Seite ueber url_launcher im '
          'System-Browser oeffnen.',
    );
  }

  @override
  PlatformWebViewCookieManager createPlatformCookieManager(
    PlatformWebViewCookieManagerCreationParams params,
  ) {
    throw PlatformException(
      code: 'unimplemented',
      message: 'WebView-Cookies gibt es auf der Nintendo Switch nicht.',
    );
  }
}

/// Wird vom generierten Horizon-Registranten aufgerufen, sobald dieser Shim
/// den READY-Marker traegt. Aktuell bewusst auskommentiert - siehe Kopf.
void registerHorizonShim() {
  // WebViewPlatform.instance = HorizonWebViewPlatform();
}