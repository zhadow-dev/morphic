import 'package:flutter/material.dart';

import 'home_glass.dart';
import 'home_test_controls.dart';

/// SPATIAL HOME — the circle surface, stripped to the counter + close probe.
///
/// `shape: 'circle'` is a COMPOSITOR mask the Flutter engine knows nothing
/// about, so the content is wrapped in [ClipOval] to keep it (and its hit area)
/// inside the visible circle rather than bleeding into the masked-away corners.
@pragma('vm:entry-point')
void homeCircleMain() => runApp(
      const HomeSurfaceApp(
        child: ClipOval(child: DiagnosticSurface(label: 'circle', vertical: true)),
      ),
    );
