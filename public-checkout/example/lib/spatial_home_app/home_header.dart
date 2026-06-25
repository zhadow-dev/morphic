import 'package:flutter/material.dart';

import 'home_glass.dart';
import 'home_test_controls.dart';

/// SPATIAL HOME — header pill, stripped to the counter + close probe.
@pragma('vm:entry-point')
void homeHeaderMain() =>
    runApp(const HomeSurfaceApp(child: DiagnosticSurface(label: 'header')));
