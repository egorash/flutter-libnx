// Template-Anwendung: zeigt, wie ein eigenes Plugin auf Horizon angebunden
// wird. Der Bildschirm ist bewusst schlank - der Wert liegt in den drei
// Kommentarstellen hier:
//
//   1. MethodChannel anlegen: Der Name unten ("plugins.flutter.io/plugin_
//      template") ist der Schluessel im Kanal-Register des Embedders
//      (embedder/src/engine/plugins_horizon.cpp, MakeRegistry()).
//   2. Aufruf mit sichtbarem Fehlerbild: bis der C++-Handler im Register
//      steht, kommt MissingPluginException - und genau das soll man sehen,
//      statt es still zu schlucken (ehrlicher Fehlerpfad).
//   3. Rueckgabewert anzeigen: getGreeting ist die Beispiel-Methode aus
//      source/handler_stub.cpp (Referenz-Handler).
//
// B-Pfad (Dart-Shim) statt C++-Handler: siehe dart_helpers/horizon_plugins/
// und build-dart-app.sh - ein Shim mit Marker // HORIZON_SHIM: READY wird
// automatisch in generated/horizon_main.dart eingebunden.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const PluginTemplateApp());
}

class PluginTemplateApp extends StatelessWidget {
  const PluginTemplateApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'plugin_template',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF4CC2FF)),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  // Kanal zum Referenz-Handler: StandardMethodCodec (voreingestellt).
  static const MethodChannel _template =
      MethodChannel('plugins.flutter.io/plugin_template');

  String _result = 'noch nicht aufgerufen';

  Future<void> _call() async {
    try {
      final String? greeting =
          await _template.invokeMethod<String>('getGreeting');
      setState(() => _result = greeting ?? '<null>');
    } on PlatformException catch (e) {
      setState(() => _result = 'PlatformException: ${e.code}');
    } on MissingPluginException {
      // Erwartet, solange der Handler nicht im Register steht (Schritt 2).
      setState(() => _result = 'Kanal nicht implementiert');
    }
  }

  @override
  Widget build(BuildContext context) {
    final ColorScheme colors = Theme.of(context).colorScheme;
    return Scaffold(
      backgroundColor: colors.surface,
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: <Widget>[
            Text(
              'Plugin-Template',
              style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    color: colors.primary,
                    fontWeight: FontWeight.bold,
                  ),
            ),
            const SizedBox(height: 24),
            Text(
              'plugins.flutter.io/plugin_template -> getGreeting()',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 48),
            FilledButton.icon(
              autofocus: true,
              onPressed: _call,
              icon: const Icon(Icons.extension),
              label: const Text(
                'Handler aufrufen',
                style: TextStyle(fontSize: 22),
              ),
              style: FilledButton.styleFrom(
                padding: const EdgeInsets.symmetric(
                  horizontal: 32,
                  vertical: 24,
                ),
              ),
            ),
            const SizedBox(height: 32),
            Text(
              'Antwort: $_result',
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    color: _result.startsWith('Kanal') ? colors.error : null,
                  ),
            ),
          ],
        ),
      ),
    );
  }
}