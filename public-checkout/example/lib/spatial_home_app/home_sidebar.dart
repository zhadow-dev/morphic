import 'package:flutter/material.dart';

import 'home_glass.dart';
import 'home_test_controls.dart';

/// SPATIAL HOME — the tall, narrow sidebar: counter + close, stacked vertically.
@pragma('vm:entry-point')
void homeSidebarMain() => runApp(
      const HomeSurfaceApp(child: DiagnosticSurface(label: 'sidebar', vertical: true)),
    );
