// Smoke test for the public Morphic authoring SDK (`package:morphic/morphic.dart`).
// Exercises the declarative surface in pure Dart — no native runtime needed —
// which also proves the package is consumable as a normal Dart import.

import 'package:flutter_test/flutter_test.dart';
import 'package:morphic/morphic.dart';

class _DemoApp extends MorphicApp {
  @override
  String get name => 'Demo';

  @override
  List<SurfaceSpec> surfaces() => const [
        SurfaceSpec.workspace(id: 'main', entrypoint: 'demoMain'),
        SurfaceSpec.inspector(id: 'info', entrypoint: 'infoMain', parent: 'main'),
      ];
}

void main() {
  test('MorphicApp declares surfaces via the public SDK', () {
    final app = _DemoApp();
    expect(app.name, 'Demo');
    final specs = app.surfaces();
    expect(specs, hasLength(2));
    expect(specs.first.kind, 'workspace');
    expect(specs.first.id, 'main');
    expect(specs.last.kind, 'inspector');
    expect(specs.last.parent, 'main');
  });

  test('SurfaceSpec named constructors set the right kind', () {
    const ws = SurfaceSpec.workspace(id: 'w', entrypoint: 'm');
    const tp = SurfaceSpec.toolPalette(id: 't', entrypoint: 'm', parent: 'w');
    const ov = SurfaceSpec.overlay(id: 'o', entrypoint: 'm');
    expect(ws.kind, 'workspace');
    expect(tp.kind, 'tool_palette');
    expect(ov.kind, 'overlay');
    expect(ov.parent, isNull);
  });
}
