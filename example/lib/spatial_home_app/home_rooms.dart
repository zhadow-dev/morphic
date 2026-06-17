import 'package:flutter/material.dart';

import 'home_glass.dart';
import 'home_test_controls.dart';

/// SPATIAL HOME — rooms pill, stripped to the counter + close probe.
@pragma('vm:entry-point')
void homeRoomsMain() =>
    runApp(const HomeSurfaceApp(child: DiagnosticSurface(label: 'rooms')));
