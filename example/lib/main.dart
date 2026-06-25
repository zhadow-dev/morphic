// Morphic quickstart — the smallest meaningful multi-window app.
//
// One Flutter app boots TWO real native windows. The first owns a value; the
// second is a separate Flutter engine that mirrors it live over `AppBus`. Neither
// window reaches into the other — they only broadcast and react.
import 'package:flutter/material.dart';
import 'package:morphic/morphic.dart';

const _count = 'count'; // the AppBus topic both windows share

// Each window is an ordinary Flutter app behind a named, tree-shake-safe
// entrypoint the runtime launches by name.
@pragma('vm:entry-point')
void mainWindow() => runApp(const _App(home: CounterWindow()));

@pragma('vm:entry-point')
void mirrorWindow() => runApp(const _App(home: MirrorWindow()));

class _App extends StatelessWidget {
  const _App({required this.home});
  final Widget home;
  @override
  Widget build(BuildContext context) => MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: ThemeData.dark(useMaterial3: true),
    home: home,
  );
}

/// Declares the two windows that boot with the app.
class QuickstartApp extends MorphicApp {
  @override
  String get name => 'Morphic Quickstart';

  @override
  List<SurfaceSpec> surfaces() => const [
    SurfaceSpec.workspace(id: 'main', entrypoint: 'mainWindow', width: 360, height: 300),
    SurfaceSpec.workspace(id: 'mirror', entrypoint: 'mirrorWindow', x: 520, width: 280, height: 300),
  ];
}

void main() => runMorphicApp(app: QuickstartApp());

/// Window 1 — owns the value and broadcasts every change.
class CounterWindow extends StatefulWidget {
  const CounterWindow({super.key});
  @override
  State<CounterWindow> createState() => _CounterWindowState();
}

class _CounterWindowState extends State<CounterWindow> {
  int _value = 0;

  void _set(int v) {
    setState(() => _value = v);
    AppBus.broadcast(_count, {'value': v}); // tell the other window
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Counter')),
    body: Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text('$_value', style: Theme.of(context).textTheme.displayMedium),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              IconButton.filledTonal(
                onPressed: () => _set(_value - 1),
                icon: const Icon(Icons.remove),
              ),
              const SizedBox(width: 16),
              IconButton.filledTonal(
                onPressed: () => _set(_value + 1),
                icon: const Icon(Icons.add),
              ),
            ],
          ),
          const SizedBox(height: 12),
          const Text('A separate native window mirrors this live →'),
        ],
      ),
    ),
  );
}

/// Window 2 — a separate engine that reacts to AppBus, never touching window 1.
class MirrorWindow extends StatefulWidget {
  const MirrorWindow({super.key});
  @override
  State<MirrorWindow> createState() => _MirrorWindowState();
}

class _MirrorWindowState extends State<MirrorWindow> {
  int _value = 0;

  @override
  void initState() {
    super.initState();
    AppBus.on(_count, (p) => setState(() => _value = (p['value'] as int?) ?? 0));
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Live mirror')),
    body: Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Text('value'),
          Text('$_value', style: Theme.of(context).textTheme.headlineMedium),
          const SizedBox(height: 12),
          const Text('doubled'),
          Text('${_value * 2}', style: Theme.of(context).textTheme.headlineMedium),
        ],
      ),
    ),
  );
}
